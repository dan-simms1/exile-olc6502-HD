#pragma once
//
// SN76489 — BBC Micro sound chip emulation.
//
// Three tone channels (square waves) + one noise channel.
// Each channel has a 4-bit attenuation (volume) and a 10-bit divider.
// Internal clock on BBC = 4 MHz / 16 = 250 kHz.
//
// Register write byte format:
//   1RRtDDDD — bit7=1 register-select; RR=channel(0-3); t=type(0=tone,1=vol);
//              DDDD = data low nibble (or 4-bit volume).
//   0sDDDDDD — bit7=0 data byte; uses last latched register; bits 5..0 = high
//              6 bits of frequency divider (or noise mode for ch3).
//
// Render() pulls audio at any sample rate; it stride-clocks the chip's 250 kHz
// internal counters using fixed-point math.

#include <atomic>
#include <cstdint>
#include <mutex>

class SN76489 {
public:
    explicit SN76489(uint32_t outputSampleRate = 22050);

    // Latch a byte into the chip (called when sound chip /WE goes low).
    void Write(uint8_t b);

    // Render N samples of signed 16-bit mono into `out`. Thread-safe vs Write().
    void Render(int16_t* out, int numSamples);

private:
    struct Channel {
        uint32_t divider = 1;     // 10-bit value (1..1023). 0 means 1024.
        uint8_t  volume  = 15;    // 4-bit attenuation; 15 = silent
        uint32_t counter = 1;     // counts down at 250 kHz
        int8_t   polarity= 1;     // +1 or -1
    };
    Channel mCh[4];
    uint16_t mNoiseLfsr = 0x8000;
    uint8_t  mNoiseCtrl = 0;       // bits 0-1 = rate, bit 2 = white(1)/periodic(0)
    int      mLatchedReg = 0;      // last 4-bit "register" the chip is waiting on

    uint32_t mOutSampleRate;
    // SN runs at 250 kHz; per output sample we advance mInternalStep = 250000/outSR
    // using fixed-point Q16 to avoid drift.
    uint32_t mStepQ16;
    uint32_t mAccumQ16 = 0;

    std::mutex mMutex;             // serialize Write() vs Render()
    int16_t mVolumeLut[16];        // attenuation table

    inline int8_t MixChannel(int ch);
    inline void   StepInternal();
};
