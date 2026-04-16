#pragma once
//
// SampleManager — loads and plays Tom Seddon's Exile sample WAVs via macOS
// AudioToolbox. Samples are the "voice" PCM data that the BBC Master enhanced
// version played from sideways RAM. We skip the 6502 sample-playback chain
// and play the WAVs directly in C++.
//
// Sample IDs (from samples/N.wav):
//   0 — "Welcome to the land of the exile"   (game start)
//   1 — "Ow!"                                 (player hurt, short)
//   2 — "Ow"                                  (player hurt, shorter)
//   3 — "Ooh"                                 (reaction)
//   4 — "Oooh!"                               (reaction)
//   5 — "Destroy!"                            (aggressive)
//   6 — "Radio die"                           (death)
//
// All samples: 7813 Hz, 8-bit unsigned, mono.

#include <string>
#include <vector>
#include <cstdint>

class SampleManager {
public:
    SampleManager();
    ~SampleManager();

    // Load all N.wav files from a directory. Returns number of samples loaded.
    int LoadDirectory(const std::string& dir);

    // Play sample by ID. Non-blocking. Overlapping plays are allowed.
    void Play(int sampleId);

    bool IsLoaded() const { return !mSamples.empty(); }

private:
    struct WavData {
        std::vector<uint8_t> pcm;    // 8-bit unsigned PCM samples
        uint32_t sampleRate = 0;
    };
    std::vector<WavData> mSamples;

    bool LoadWav(const std::string& path, WavData& out);
};
