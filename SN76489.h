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
        uint32_t divider  = 1;    // 10-bit value (1..1024). 0 stored as 1024.
        uint8_t  volume   = 15;   // 4-bit attenuation; 15 = silent
        double   counter  = 1.0;  // fractional counter, decrements per output sample
        uint8_t  outputBit = 0;   // 0 or 1 (jsbeeb-style unipolar output)
    };
    Channel mCh[4];
    uint16_t mNoiseLfsr = 0x4000;
    uint8_t  mNoiseCtrl = 0;       // bits 0-1 = rate, bit 2 = white(1)/periodic(0)
    int      mLatchedReg = 0;      // last 4-bit "register" the chip is waiting on

    uint32_t mOutSampleRate;
    // jsbeeb-style: counter decrements by waveDecrementPerSecond/sampleRate
    // each output sample. waveDecrementPerSecond = 250 kHz (the BBC chip's
    // internal counter tick rate after the /16 prescaler).
    double mSampleDecrement;

    std::mutex mMutex;             // serialize Write() vs Render()
    float mVolumeLut[16];          // unipolar volume table (max 0.25, mixed peak ~1.0)

    // DC-blocker high-pass filter state. Real BBC's audio output is
    // AC-coupled (capacitor); without this our output has a significant
    // DC component (silence at unipolar 0 shifts to -0.5 bipolar = -16383),
    // causing audible clicks whenever channels transition between active
    // and silent. Classic y[n] = x[n] - x[n-1] + alpha * y[n-1].
    float mDcPrevIn  = 0.0f;
    float mDcPrevOut = 0.0f;

    inline void ShiftLfsr();
    inline bool DoChannelStep(int ch, uint32_t addAmount);
};
