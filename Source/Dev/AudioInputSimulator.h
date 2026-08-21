#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Substitutes loaded-from-disk audio for live mic input, so AudioTrack
// recording can be exercised without talking into a mic.
class AudioInputSimulator
{
public:
    void start (std::vector<float> samples)
    {
        const juce::ScopedLock lock (dataLock);
        data = std::move (samples);
        playbackIndex = 0;
        active.store (! data.empty());
    }

    bool isActive() const { return active.load(); }

    void fillNextBlock (float* destination, int numSamples)
    {
        const juce::ScopedLock lock (dataLock);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = playbackIndex < data.size() ? data[playbackIndex++] : 0.0f;

        if (playbackIndex >= data.size())
            active.store (false);
    }

private:
    juce::CriticalSection dataLock;
    std::vector<float> data;
    size_t playbackIndex = 0;
    std::atomic<bool> active { false };
};
