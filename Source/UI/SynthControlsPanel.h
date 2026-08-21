#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"

// Bordered, titled section of the track inspector reserved for
// synth-specific controls - oscillator waveform for now, with ADSR/filter/
// effect knobs meant to land here too as they're built. Drawn as its own
// panel (background + border + "Synth" header) so the reserved space next
// to the scope reads as a defined section rather than blank canvas.
class SynthControlsPanel : public juce::Component
{
public:
    SynthControlsPanel();

    std::function<void (OscillatorWaveform)> onWaveformChanged;

    void setWaveform (OscillatorWaveform waveform);
    void setControlsEnabled (bool shouldBeEnabled);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label titleLabel;
    juce::Label waveformLabel;
    juce::ComboBox waveformBox;
};
