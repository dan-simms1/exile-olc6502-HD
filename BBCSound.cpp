#include "BBCSound.h"

#include <AudioToolbox/AudioToolbox.h>
#include <cstdio>
#include <cstring>

namespace {
    constexpr uint32_t kSampleRate = 22050;
    constexpr UInt32   kNumBuffers  = 3;
    constexpr UInt32   kFramesPerBuffer = 256;    // ~12 ms of audio per buffer
                                                  // (3 × 12 = ~35 ms total latency)
    constexpr UInt32   kBytesPerFrame = sizeof(int16_t);
    constexpr UInt32   kBufferBytes = kFramesPerBuffer * kBytesPerFrame;

    struct AudioState {
        AudioQueueRef        queue = nullptr;
        AudioQueueBufferRef  buffers[kNumBuffers] = {};
        SN76489*             chip = nullptr;
        bool                 running = false;
    };

    void OnBufferEmpty(void* userData, AudioQueueRef aq, AudioQueueBufferRef buf) {
        auto* s = static_cast<AudioState*>(userData);
        if (!s || !s->running) return;
        s->chip->Render(reinterpret_cast<int16_t*>(buf->mAudioData), kFramesPerBuffer);
        buf->mAudioDataByteSize = kBufferBytes;
        AudioQueueEnqueueBuffer(aq, buf, 0, nullptr);
    }
}

BBCSound::BBCSound() : mChip(kSampleRate) {}

BBCSound::~BBCSound() {
    Stop();
    if (mAudioState) {
        delete static_cast<AudioState*>(mAudioState);
        mAudioState = nullptr;
    }
}

void BBCSound::Start() {
    if (mAudioState) return;
    auto* s = new AudioState();
    s->chip = &mChip;
    mAudioState = s;

    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate       = (Float64)kSampleRate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket   = kBytesPerFrame;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = kBytesPerFrame;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel   = 16;

    OSStatus st = AudioQueueNewOutput(&fmt, OnBufferEmpty, s,
                                      nullptr, kCFRunLoopCommonModes, 0, &s->queue);
    if (st != noErr || !s->queue) {
        fprintf(stderr, "BBCSound: AudioQueueNewOutput failed (%d)\n", (int)st);
        return;
    }

    // Pre-fill buffers with silence (chip starts with vol=15 = silent)
    // and enqueue them all.
    for (UInt32 i = 0; i < kNumBuffers; ++i) {
        st = AudioQueueAllocateBuffer(s->queue, kBufferBytes, &s->buffers[i]);
        if (st != noErr) {
            fprintf(stderr, "BBCSound: AudioQueueAllocateBuffer %u failed (%d)\n", i, (int)st);
            return;
        }
        memset(s->buffers[i]->mAudioData, 0, kBufferBytes);
        s->buffers[i]->mAudioDataByteSize = kBufferBytes;
        AudioQueueEnqueueBuffer(s->queue, s->buffers[i], 0, nullptr);
    }

    s->running = true;
    AudioQueueStart(s->queue, nullptr);
    fprintf(stderr, "BBCSound: SN76489 stream started (%u Hz, %u-frame buffers)\n",
            kSampleRate, kFramesPerBuffer);
}

void BBCSound::Stop() {
    auto* s = static_cast<AudioState*>(mAudioState);
    if (!s || !s->running) return;
    s->running = false;
    if (s->queue) {
        AudioQueueStop(s->queue, true);
        AudioQueueDispose(s->queue, true);
        s->queue = nullptr;
    }
}

void BBCSound::OnPortAWrite(uint8_t value) {
    mPortA = value;
}

void BBCSound::OnPortBWrite(uint8_t value) {
    // IC32 addressable latch:
    //   value bits 0..2 = line address (0..7)
    //   value bit  3    = data to set on that line
    int line = value & 0x07;
    int data = (value >> 3) & 0x01;
    if (line == 0) {
        // Line 0 = sound chip /WE.
        // Falling edge (1 → 0) latches the byte currently on port A into the chip.
        if (mIc32SoundWE == 1 && data == 0) {
            mChip.Write(mPortA);
        }
        mIc32SoundWE = (uint8_t)data;
    }
}
