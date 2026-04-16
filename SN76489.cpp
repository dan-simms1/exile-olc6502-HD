#include "SN76489.h"

#include <cmath>
#include <cstring>

SN76489::SN76489(uint32_t outputSampleRate)
    : mOutSampleRate(outputSampleRate)
{
    // 250 kHz internal / outSR, scaled to Q16 fixed-point.
    mStepQ16 = (uint32_t)((250000ULL << 16) / outputSampleRate);

    // 16-step ~2 dB per step attenuation table for one channel. With four
    // channels mixed, peak amplitude is ~4 × 4096 = 16384 — leaves headroom.
    for (int i = 0; i < 16; ++i) {
        if (i == 15) {
            mVolumeLut[i] = 0;
        } else {
            // -2 dB per step → factor = pow(10, -2/20)
            double level = 4096.0 * std::pow(10.0, -2.0 * i / 20.0);
            mVolumeLut[i] = (int16_t)level;
        }
    }
}

void SN76489::Write(uint8_t b) {
    std::lock_guard<std::mutex> lk(mMutex);

    if (b & 0x80) {
        // Register-select byte: 1RRtDDDD
        int chan  = (b >> 5) & 0x03;
        int isVol = (b >> 4) & 0x01;
        int data  = b & 0x0F;
        mLatchedReg = (chan << 1) | isVol;

        if (isVol) {
            mCh[chan].volume = (uint8_t)data;
        } else if (chan == 3) {
            // Noise channel control register
            mNoiseCtrl  = (uint8_t)(data & 0x07);
            mNoiseLfsr  = 0x4000;  // jsbeeb: reset to 1<<14
        } else {
            // Tone channel: replace low 4 bits of divider, preserve high 6.
            // Divider 0 is treated as 1024 (jsbeeb / SN76489 spec) so silenced
            // channels don't suddenly run at maximum frequency.
            uint32_t hi = (mCh[chan].divider >> 4) & 0x3F;
            uint32_t v  = (hi << 4) | data;
            mCh[chan].divider = v ? v : 1024;
        }
    } else {
        // Data byte: 0xDDDDDDDD updates high 6 bits of last latched tone reg.
        int chan  = (mLatchedReg >> 1) & 0x03;
        int isVol = mLatchedReg & 0x01;
        int data  = b & 0x3F;
        if (isVol) {
            // Spec says vol bytes are 4-bit only via reg-select; ignore.
        } else if (chan == 3) {
            mNoiseCtrl  = (uint8_t)(data & 0x07);
            mNoiseLfsr  = 0x4000;
        } else {
            uint32_t lo = mCh[chan].divider & 0x0F;
            uint32_t v  = ((uint32_t)data << 4) | lo;
            mCh[chan].divider = v ? v : 1024;
        }
    }
}

inline void SN76489::StepInternal() {
    // Advance one 250 kHz tick.
    for (int c = 0; c < 3; ++c) {
        if (--mCh[c].counter == 0) {
            mCh[c].counter = mCh[c].divider;
            mCh[c].polarity = -mCh[c].polarity;
        }
    }
    // Noise channel uses rate from mNoiseCtrl bits 0..1 OR ch2 frequency.
    uint32_t noiseDiv;
    switch (mNoiseCtrl & 0x03) {
        case 0: noiseDiv = 16;  break;   // ÷512 of 8MHz becomes ÷16 of 250kHz
        case 1: noiseDiv = 32;  break;
        case 2: noiseDiv = 64;  break;
        default: noiseDiv = mCh[2].divider; break;
    }
    if (--mCh[3].counter == 0) {
        mCh[3].counter = noiseDiv ? noiseDiv : 1;
        // Shift the LFSR. Per jsbeeb's BBC SN76489 implementation:
        //   white noise   = tap 0 XOR tap 1   (NOT 0 XOR 3 like SMS variant)
        //   periodic noise = tap 0 alone
        // 15-bit shift register; feedback into bit 14.
        unsigned outBit = mNoiseLfsr & 1;
        unsigned newBit;
        if (mNoiseCtrl & 0x04) {
            newBit = ((mNoiseLfsr) ^ (mNoiseLfsr >> 1)) & 1;
        } else {
            newBit = mNoiseLfsr & 1;
        }
        mNoiseLfsr = (uint16_t)((mNoiseLfsr >> 1) | (newBit << 14));
        mCh[3].polarity = (outBit ? -1 : 1);
    }
}

inline int8_t SN76489::MixChannel(int ch) {
    return mCh[ch].polarity;  // ±1
}

void SN76489::Render(int16_t* out, int numSamples) {
    std::lock_guard<std::mutex> lk(mMutex);

    for (int i = 0; i < numSamples; ++i) {
        // Step the internal counters by mStepQ16 (250kHz / outSR) ticks.
        mAccumQ16 += mStepQ16;
        while (mAccumQ16 >= 0x10000) {
            StepInternal();
            mAccumQ16 -= 0x10000;
        }
        int32_t mix = 0;
        for (int c = 0; c < 4; ++c) {
            mix += mCh[c].polarity * mVolumeLut[mCh[c].volume];
        }
        // Soft clip to int16 range
        if (mix >  32767) mix =  32767;
        if (mix < -32768) mix = -32768;
        out[i] = (int16_t)mix;
    }
}
