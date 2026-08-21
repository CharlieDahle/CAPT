#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"

// The real-time-relevant contract AudioEngine needs from a track - no
// juce::Component/GUI dependency at all, unlike TrackBase (which is both
// this AND a Component). AudioEngine only ever sees tracks through this
// interface, so it has no compile-time coupling to the GUI toolkit despite
// the concrete track objects being Components too.
class TrackAudioSource
{
public:
    virtual ~TrackAudioSource() = default;

    virtual void prepareToPlay (double sampleRate) = 0;

    // inputBuffer is this block's raw mic input, nullptr if unavailable;
    // only AudioTrack uses it. bpm is the current tempo, read fresh every
    // block - only MidiTrack uses it (to convert between real time and beat
    // positions); AudioTrack ignores it since recorded audio isn't
    // tempo-relative.
    virtual void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                   TransportState globalState, double elapsedSamples, double bpm,
                                   const juce::AudioBuffer<const float>* inputBuffer) = 0;

    virtual float getVolume() const = 0;
    virtual bool isArmed() const = 0;
    virtual bool isMuted() const = 0;
    virtual bool isSoloed() const = 0;
};
