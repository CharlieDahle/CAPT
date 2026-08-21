#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "EnvelopeVisualizer.h"

// Bordered, titled section of the track inspector reserved for
// synth-specific controls - oscillator waveform and its ADSR envelope, with
// filter/effect knobs meant to land here too as they're built. Drawn as its
// own panel (background + border + "Synth" header) so the reserved space
// next to the scope reads as a defined section rather than blank canvas.
class SynthControlsPanel : public juce::Component
{
public:
    SynthControlsPanel();

    std::function<void (OscillatorWaveform)> onWaveformChanged;
    std::function<void (EnvelopeParams)> onEnvelopeChanged;

    void setWaveform (OscillatorWaveform waveform);
    void setEnvelope (EnvelopeParams params);
    void setControlsEnabled (bool shouldBeEnabled);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Label waveformLabel;
    juce::ComboBox waveformBox;

    juce::Label attackLabel, decayLabel, sustainLabel, releaseLabel;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;

    // Live shape preview of the ADSR knobs above - see EnvelopeVisualizer's
    // own comment for why this isn't just the WaveformDisplay scope reused.
    EnvelopeVisualizer envelopeVisualizer;
};
