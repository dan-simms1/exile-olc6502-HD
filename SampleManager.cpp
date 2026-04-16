#include "SampleManager.h"

#include <AudioToolbox/AudioToolbox.h>
#include <mach-o/dyld.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

SampleManager::SampleManager() {}
SampleManager::~SampleManager() {}

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
    return true;
}

static bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

static std::string ExecutableDir() {
    char buf[1024];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    std::string p(buf);
    auto pos = p.find_last_of('/');
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
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
    return loaded;
}

namespace {
    // AudioQueue callback: self-destructs queue when the single enqueued buffer finishes.
    // We over-allocate a context to carry the queue pointer back to itself.
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

void SampleManager::Play(int sampleId) {
    if (sampleId < 0 || sampleId >= (int)mSamples.size()) return;
    const auto& s = mSamples[sampleId];
    if (s.pcm.empty()) return;

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

    AudioQueueBufferRef buf = nullptr;
    st = AudioQueueAllocateBuffer(aq, (UInt32)s.pcm.size(), &buf);
    if (st != noErr || !buf) { AudioQueueDispose(aq, true); delete ctx; return; }
    memcpy(buf->mAudioData, s.pcm.data(), s.pcm.size());
    buf->mAudioDataByteSize = (UInt32)s.pcm.size();

    AudioQueueEnqueueBuffer(aq, buf, 0, nullptr);
    AudioQueueStart(aq, nullptr);
}
