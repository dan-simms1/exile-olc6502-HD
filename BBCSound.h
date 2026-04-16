#pragma once
//
// BBCSound — owns the SN76489 chip emulator and a continuous CoreAudio
// AudioQueue stream. Bus.cpp routes System VIA writes here when the IC32
// addressable latch deasserts the sound chip /WE line.
//

#include "SN76489.h"
#include <cstdint>

class BBCSound {
public:
    BBCSound();
    ~BBCSound();

    // Start the continuous audio output.
    void Start();
    void Stop();

    // System VIA wiring. The Bus calls these when the corresponding mapped
    // I/O location is written.
    //   Port A latch:  the byte sent to the SN76489 when /WE is asserted.
    //   Port B latch:  IC32 addressable latch — bit3=value, bits2..0=line.
    //                  Line 0 = sound chip /WE (active low).
    void OnPortAWrite(uint8_t value);
    void OnPortBWrite(uint8_t value);

private:
    SN76489 mChip;

    uint8_t mPortA = 0;          // last byte written to System VIA port A
    uint8_t mIc32SoundWE = 1;    // current state of IC32 line 0; default high

    // Opaque AudioQueue state — kept as void* so this header doesn't drag
    // in <AudioToolbox/AudioToolbox.h> for everyone.
    void* mAudioState = nullptr;
};
