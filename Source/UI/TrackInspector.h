#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Tracks/TrackBase.h"
#include "WaveformDisplay.h"
#include "SynthControlsPanel.h"

// One shared panel for the selected track's controls, rather than each
// track lane carrying its own full control set.
class TrackInspector : public juce::Component
{
public:
    TrackInspector();

    void showTrack (TrackBase* track);
    void setGridSettingsProvider (std::function<GridSettings()> provider) { gridSettingsProvider = std::move (provider); }

    // AudioEngine pushes the selected track's raw audio here - exposed as
    // AudioTap (not WaveformDisplay) so callers only see the real-time-safe
    // sink, not the Component underneath.
    AudioTap& getWaveformTap() { return waveformDisplay; }
    void clearWaveform() { waveformDisplay.clear(); }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::darkslategrey.darker (0.3f)); }
    void resized() override;

private:
    TrackBase* currentTrack = nullptr;

    juce::Label headerLabel;
    juce::TextButton armButton;
    juce::TextButton typeButton;
    juce::TextButton quantizeButton;
    juce::Label volumeLabel;
    juce::Slider volumeSlider;
    WaveformDisplay waveformDisplay;
    SynthControlsPanel synthPanel;

    std::function<GridSettings()> gridSettingsProvider;
};
