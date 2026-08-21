#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// Real-time-safe sink for a copy of one track's rendered audio - lets
// AudioEngine feed a UI-level visualiser without depending on any concrete
// GUI class, mirroring how TrackAudioSource keeps AudioEngine decoupled from
// the concrete track types. Implementations must be safe to call from the
// audio thread: no locks, no allocation.
class AudioTap
{
public:
    virtual ~AudioTap() = default;

    virtual void pushBuffer (const juce::AudioBuffer<float>& buffer) = 0;
};
