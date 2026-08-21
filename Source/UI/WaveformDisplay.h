#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Audio/AudioTap.h"

// Scrolling oscilloscope view of whichever track AudioEngine is currently
// tapping - a thin wrapper around JUCE's built-in AudioVisualiserComponent
// that implements AudioTap so the audio thread can push samples straight
// into it (AudioVisualiserComponent's pushBuffer() is itself lock-free/
// allocation-free, safe to call from a real-time callback).
class WaveformDisplay : public juce::Component, public AudioTap
{
public:
    WaveformDisplay();

    void pushBuffer (const juce::AudioBuffer<float>& buffer) override { scope.pushBuffer (buffer); }

    // Called when the tapped track changes, so the old track's waveform
    // doesn't linger on screen labelled as the new one's.
    void clear() { scope.clear(); }

    void resized() override { scope.setBounds (getLocalBounds()); }

private:
    juce::AudioVisualiserComponent scope { 1 };
};
