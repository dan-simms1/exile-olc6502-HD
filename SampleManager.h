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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class SampleManager {
public:
    SampleManager();
    ~SampleManager();

    // Load all N.wav files from a directory. Returns number of samples loaded.
    int LoadDirectory(const std::string& dir);

    // Enqueue sample by ID. Non-blocking — actual audio init runs on a
    // background worker thread so the game loop never stalls on
    // AudioQueueStart (which can take 50-200 ms cold).
    void Play(int sampleId);

    bool IsLoaded() const { return !mSamples.empty(); }

private:
    struct WavData {
        std::vector<uint8_t> pcm;    // 8-bit unsigned PCM samples
        uint32_t sampleRate = 0;
        double durationSec = 0.0;    // for cooldown / throttling
    };
    std::vector<WavData> mSamples;
    // Per-sample "last played" timestamp (steady_clock ms). Skip Play() while
    // the previous instance of the same sample is still likely sounding,
    // mirroring the enhanced ROM's single-channel sample DAC behaviour.
    std::vector<int64_t> mLastPlayMs;

    bool LoadWav(const std::string& path, WavData& out);
    void WorkerLoop();
    void PlayImpl(int sampleId);   // blocking implementation, runs on worker

    // Worker thread + queue for non-blocking Play()
    std::thread mWorker;
    std::mutex mMutex;
    std::condition_variable mCV;
    std::queue<int> mPending;
    std::atomic<bool> mShutdown{false};
};
