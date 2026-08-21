#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

// Synthesizes a click on each beat (same phase-accumulator approach
// SynthVoice uses for track playback, so no sample assets are needed).
// Owned by AudioEngine and rendered directly into the output buffer,
// independent of any track's mute/solo - it's a monitoring aid, not part of
// the mix, and it never touches a track's input path so it can't bleed into
// a recording.
class Metronome
{
public:
    void prepareToPlay (double sampleRate);

    void setEnabled (bool shouldBeEnabled) { enabled.store (shouldBeEnabled); }
    bool isEnabled() const { return enabled.load(); }
    void setGain (float newGain) { gain.store (newGain); }

    // transportRunning = currentState != Idle. Called once per audio block
    // from AudioEngine, after elapsedSamples for this block is resolved.
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int numSamples,
                           double elapsedSamplesAtBlockStart, bool transportRunning,
                           double bpm, int timeSignatureNumerator);

private:
    void triggerClick (bool isDownbeat);

    std::atomic<bool> enabled { false };
    std::atomic<float> gain { 0.3f };
    double sampleRate = 44100.0;

    bool wasRunning = false;
    double nextBeatSampleTime = 0.0;
    juce::int64 beatCounter = 0;

    int remainingClickSamples = 0;
    int clickLengthSamples = 0;
    double clickPhase = 0.0;
    double clickPhaseDelta = 0.0;
};
