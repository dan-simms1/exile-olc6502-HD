#include "SN76489.h"

#include <cmath>
#include <cstring>

SN76489::SN76489(uint32_t outputSampleRate)
    : mOutSampleRate(outputSampleRate)
{
    // jsbeeb's waveDecrementPerSecond = soundchipFreq/2 = 250 kHz. Each
    // output sample, every channel's fractional counter decrements by
    // (250000 / outSR). When it goes negative, reload (add divider) and
    // toggle outputBit.
    mSampleDecrement = 250000.0 / (double)outputSampleRate;

    // jsbeeb volume table: each entry = pow(10, -0.1*i) / 4
    // → vol 0 = 0.25 (max), vol 14 = 0.0114, vol 15 = 0.
    // 4 channels at max sum to 1.0 = full Float32 range.
    for (int i = 0; i < 16; ++i) {
        if (i == 15) mVolumeLut[i] = 0.0f;
        else         mVolumeLut[i] = (float)(std::pow(10.0, -0.1 * i) / 4.0);
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

inline void SN76489::ShiftLfsr() {
    // BBC SN76489AN: white noise = tap 0 XOR tap 1; periodic = tap 0.
    // 15-bit shift register; feedback into bit 14.
    unsigned newBit;
    if (mNoiseCtrl & 0x04) {
        newBit = (mNoiseLfsr ^ (mNoiseLfsr >> 1)) & 1;
    } else {
        newBit = mNoiseLfsr & 1;
    }
    mNoiseLfsr = (uint16_t)((mNoiseLfsr >> 1) | (newBit << 14));
}

// jsbeeb-style: counter decrements per OUTPUT SAMPLE (not per chip tick).
// When it crosses zero, reload by adding `addAmount` and toggle outputBit.
// Returns true if the bit toggled this sample (used by noise to shift LFSR).
inline bool SN76489::DoChannelStep(int ch, uint32_t addAmount) {
    double newValue = mCh[ch].counter - mSampleDecrement;
    if (newValue < 0.0) {
        // Re-add divider; max(0,...) guards against giant negative on first
        // tick after a register write.
        double reloaded = newValue + (double)addAmount;
        if (reloaded < 0.0) reloaded = 0.0;
        mCh[ch].counter   = reloaded;
        mCh[ch].outputBit ^= 1;
        return true;
    }
    mCh[ch].counter = newValue;
    return false;
}

void SN76489::Render(int16_t* out, int numSamples) {
    std::lock_guard<std::mutex> lk(mMutex);

    for (int i = 0; i < numSamples; ++i) {
        float sample = 0.0f;

        // Three tone channels.
        for (int c = 0; c < 3; ++c) {
            DoChannelStep(c, mCh[c].divider);
            sample += (float)mCh[c].outputBit * mVolumeLut[mCh[c].volume];
        }

        // Noise channel — addAmount picks rate or borrows tone-2 divider.
        uint32_t noiseAdd;
        switch (mNoiseCtrl & 0x03) {
            case 0:  noiseAdd = 0x10; break;
            case 1:  noiseAdd = 0x20; break;
            case 2:  noiseAdd = 0x40; break;
            default: noiseAdd = mCh[2].divider; break;
        }
        if (DoChannelStep(3, noiseAdd)) {
            // Toggled this sample → shift LFSR
            ShiftLfsr();
        }
        sample += (float)(mNoiseLfsr & 1) * mVolumeLut[mCh[3].volume];

        // sample is unipolar 0..1; run through a DC-blocker high-pass
        // filter (real BBC's audio output is AC-coupled by a capacitor).
        // Eliminates periodic click/thud from DC level jumps whenever
        // channels transition between active and silent.
        //   y[n] = x[n] - x[n-1] + alpha * y[n-1]
        // alpha = 0.9975 → ~18 Hz cutoff at 44100 Hz — below audible.
        //
        // DC removal halves the effective peak amplitude of unipolar
        // square waves (their DC component is half the peak-to-peak),
        // so compensate with 2× post-filter gain. Net result matches
        // the previous centred bipolar output's amplitude but without
        // the -16383 DC pedestal.
        constexpr float kAlpha = 0.9975f;
        constexpr float kPostGain = 2.0f;
        float dc = sample - mDcPrevIn + kAlpha * mDcPrevOut;
        mDcPrevIn  = sample;
        mDcPrevOut = dc;
        int32_t s = (int32_t)(dc * 32767.0f * kPostGain);
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        out[i] = (int16_t)s;
    }
}
