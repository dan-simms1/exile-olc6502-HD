#include "SampleManager.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

static int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

SampleManager::SampleManager() {
    // Start the audio worker so Play() never blocks the game loop.
    mWorker = std::thread([this]() { WorkerLoop(); });
}

SampleManager::~SampleManager() {
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mShutdown = true;
    }
    mCV.notify_all();
    if (mWorker.joinable()) mWorker.join();
}

bool SampleManager::LoadWav(const std::string& path, WavData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
    // Minimum WAV header
    if (buf.size() < 44) return false;
    if (memcmp(&buf[0], "RIFF", 4) != 0) return false;
    if (memcmp(&buf[8], "WAVE", 4) != 0) return false;

    // Walk chunks to find fmt and data
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    size_t pos = 12;
    size_t dataOff = 0, dataLen = 0;
    while (pos + 8 <= buf.size()) {
        const char* id = (const char*)&buf[pos];
        uint32_t sz = buf[pos+4] | (buf[pos+5] << 8) | (buf[pos+6] << 16) | (buf[pos+7] << 24);
        pos += 8;
        if (memcmp(id, "fmt ", 4) == 0) {
            if (sz >= 16) {
                channels      = buf[pos+2] | (buf[pos+3] << 8);
                sampleRate    = buf[pos+4] | (buf[pos+5] << 8) | (buf[pos+6] << 16) | (buf[pos+7] << 24);
                bitsPerSample = buf[pos+14] | (buf[pos+15] << 8);
            }
        } else if (memcmp(id, "data", 4) == 0) {
            dataOff = pos;
            dataLen = sz;
            break;
        }
        pos += sz;
    }
    if (!dataLen || channels != 1 || bitsPerSample != 8) {
        fprintf(stderr, "SampleManager: %s unsupported (ch=%u, bps=%u, len=%zu)\n",
                path.c_str(), channels, bitsPerSample, dataLen);
        return false;
    }
    out.pcm.assign(buf.begin() + dataOff, buf.begin() + dataOff + dataLen);
    out.sampleRate = sampleRate;
    out.durationSec = (sampleRate > 0) ? (double)dataLen / (double)sampleRate : 0.0;
    return true;
}

static bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string ExecutableDir() {
#if defined(__APPLE__)
    char buf[1024];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    std::string p(buf);
    auto pos = p.find_last_of('/');
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
#elif defined(__linux__)
    char buf[1024];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string p(buf);
    auto pos = p.find_last_of('/');
    return pos == std::string::npos ? "" : p.substr(0, pos);
#else
    return "";  // Windows: TODO — use GetModuleFileName
#endif
}

int SampleManager::LoadDirectory(const std::string& dirHint) {
    // Try the hint first, then sensible fall-backs so both terminal launches
    // (CWD = repo) and .app-bundle launches (CWD = /) find the WAVs:
    //   1. CWD/<hint>            (repo run from terminal)
    //   2. <exe_dir>/<hint>      (binary copied somewhere with samples next to it)
    //   3. <exe_dir>/../Resources/<hint>  (proper .app bundle layout)
    std::string exeDir = ExecutableDir();
    std::vector<std::string> candidates = {
        dirHint,
        exeDir + "/" + dirHint,
        exeDir + "/../Resources/" + dirHint,
    };
    std::string dir;
    for (const auto& c : candidates) {
        if (FileExists(c + "/0.wav")) { dir = c; break; }
    }
    if (dir.empty()) {
        fprintf(stderr, "SampleManager: could not find samples in any of:\n");
        for (const auto& c : candidates) fprintf(stderr, "  %s\n", c.c_str());
        return 0;
    }

    mSamples.clear();
    for (int i = 0; i < 7; ++i) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%d.wav", dir.c_str(), i);
        WavData w;
        if (!LoadWav(path, w)) {
            fprintf(stderr, "SampleManager: could not load %s\n", path);
            mSamples.push_back({});
            continue;
        }
        mSamples.push_back(std::move(w));
    }
    int loaded = 0;
    for (auto& s : mSamples) if (!s.pcm.empty()) ++loaded;
    fprintf(stderr, "SampleManager: loaded %d/%zu samples from %s\n",
            loaded, mSamples.size(), dir.c_str());
    mLastPlayMs.assign(mSamples.size(), 0);
    return loaded;
}

#if defined(__APPLE__)
namespace {
    // AudioQueue callback: self-destructs queue when the single enqueued buffer finishes.
    struct PlayCtx {
        AudioQueueRef q;
    };
    static void OnAQBuffer(void* userData, AudioQueueRef aq, AudioQueueBufferRef buf) {
        (void)buf;
        auto* ctx = static_cast<PlayCtx*>(userData);
        AudioQueueStop(aq, true);
        AudioQueueDispose(aq, true);
        delete ctx;
    }
}
#endif

void SampleManager::Play(int sampleId) {
    if (sampleId < 0 || sampleId >= (int)mSamples.size()) return;
    const auto& s = mSamples[sampleId];
    if (s.pcm.empty()) return;
    // Cooldown: don't re-trigger the same sample while its previous instance
    // is still playing (mirrors enhanced ROM's single-channel sample DAC).
    int64_t now = NowMs();
    int64_t cooldownMs = (int64_t)(s.durationSec * 1000.0);
    if (cooldownMs > 0 && (now - mLastPlayMs[sampleId]) < cooldownMs) return;
    mLastPlayMs[sampleId] = now;
    QueueEntry e;
    e.sampleId = sampleId;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mQueue.size() < 32) mQueue.push(std::move(e));
    }
    mCV.notify_one();
}

void SampleManager::PlayTone(double freqHz, double ampLin, int durationMs) {
    if (durationMs <= 0) return;
    if (ampLin <= 0.0) return;
    if (ampLin > 1.0) ampLin = 1.0;

    // Synthesize into 22050 Hz 8-bit unsigned PCM. Square wave for tones,
    // pseudo-noise for freqHz<=0 (used for shots/explosions — SN76489 chan 4).
    constexpr uint32_t SR = 22050;
    const int nSamples = (int)((int64_t)SR * durationMs / 1000);
    auto* w = new WavData();
    w->sampleRate = SR;
    w->durationSec = (double)nSamples / (double)SR;
    w->pcm.resize(nSamples);

    const int hi = (int)(127.0 + 127.0 * ampLin);
    const int lo = (int)(127.0 - 127.0 * ampLin);

    if (freqHz > 0.0) {
        // Square wave at freqHz with a short linear fade-out on the tail
        // so cutoff doesn't click.
        const double period = (double)SR / freqHz;     // in samples
        const int fadeStart = (int)(nSamples * 0.85);  // last 15% fades
        for (int i = 0; i < nSamples; ++i) {
            int phase = (int)std::fmod((double)i, period);
            int v = (phase < period / 2.0) ? hi : lo;
            if (i > fadeStart) {
                double k = 1.0 - (double)(i - fadeStart) / (double)(nSamples - fadeStart);
                v = (int)(127.0 + (v - 127) * k);
            }
            w->pcm[i] = (uint8_t)v;
        }
    } else {
        // White noise with fade-out (SN76489 channel-4 style).
        uint32_t lfsr = 0xACE1u;
        const int fadeStart = (int)(nSamples * 0.85);
        for (int i = 0; i < nSamples; ++i) {
            // Galois 16-bit LFSR, taps 0xB400
            unsigned bit = lfsr & 1;
            lfsr >>= 1;
            if (bit) lfsr ^= 0xB400u;
            int v = bit ? hi : lo;
            if (i > fadeStart) {
                double k = 1.0 - (double)(i - fadeStart) / (double)(nSamples - fadeStart);
                v = (int)(127.0 + (v - 127) * k);
            }
            w->pcm[i] = (uint8_t)v;
        }
    }

    QueueEntry e;
    e.sampleId = -1;
    e.tone = w;
    {
        std::lock_guard<std::mutex> lk(mMutex);
        if (mQueue.size() < 32) {
            mQueue.push(std::move(e));
        } else {
            delete w;  // drop silently if queue is saturated
            return;
        }
    }
    mCV.notify_one();
}

void SampleManager::WorkerLoop() {
    while (true) {
        QueueEntry e;
        {
            std::unique_lock<std::mutex> lk(mMutex);
            mCV.wait(lk, [&]{ return mShutdown || !mQueue.empty(); });
            if (mShutdown && mQueue.empty()) return;
            e = std::move(mQueue.front());
            mQueue.pop();
        }
        if (e.tone) {
            PlayData(*e.tone, 1.0f);         // synthesized tones at unity
            delete e.tone;
        } else if (e.sampleId >= 0 && e.sampleId < (int)mSamples.size()) {
            // Voice samples attenuated — raw 8-bit PCM from Tom Seddon's
            // extraction peaks at full ±127, which booms above the chip's
            // square-wave output. 0.4 keeps speech audible but at a level
            // closer to the chip's beeps/shots so they don't overpower.
            PlayData(mSamples[e.sampleId], 0.4f);
        }
    }
}

void SampleManager::PlayData(const WavData& s, float gain) {
#if defined(__APPLE__)
    AudioStreamBasicDescription desc = {};
    desc.mSampleRate       = (Float64)s.sampleRate;
    desc.mFormatID         = kAudioFormatLinearPCM;
    desc.mFormatFlags      = 0;  // 8-bit unsigned
    desc.mBytesPerPacket   = 1;
    desc.mFramesPerPacket  = 1;
    desc.mBytesPerFrame    = 1;
    desc.mChannelsPerFrame = 1;
    desc.mBitsPerChannel   = 8;

    auto* ctx = new PlayCtx();
    AudioQueueRef aq = nullptr;
    OSStatus st = AudioQueueNewOutput(&desc, OnAQBuffer, ctx,
                                      nullptr, kCFRunLoopCommonModes, 0, &aq);
    if (st != noErr || !aq) { delete ctx; return; }
    ctx->q = aq;

    // Apply per-play gain via AudioQueue's volume parameter (0..1).
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    AudioQueueSetParameter(aq, kAudioQueueParam_Volume, gain);

    AudioQueueBufferRef buf = nullptr;
    st = AudioQueueAllocateBuffer(aq, (UInt32)s.pcm.size(), &buf);
    if (st != noErr || !buf) { AudioQueueDispose(aq, true); delete ctx; return; }
    memcpy(buf->mAudioData, s.pcm.data(), s.pcm.size());
    buf->mAudioDataByteSize = (UInt32)s.pcm.size();

    AudioQueueEnqueueBuffer(aq, buf, 0, nullptr);
    AudioQueueStart(aq, nullptr);
#else
    // No audio backend on this platform.
    (void)s; (void)gain;
#endif
}
