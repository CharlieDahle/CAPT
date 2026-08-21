#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Tracks/TrackBase.h"

// One shared panel for the selected track's controls, rather than each
// track lane carrying its own full control set.
class TrackInspector : public juce::Component
{
public:
    TrackInspector();

    void showTrack (TrackBase* track);

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::darkslategrey.darker (0.3f)); }
    void resized() override;

private:
    TrackBase* currentTrack = nullptr;

    juce::Label headerLabel;
    juce::TextButton armButton;
    juce::TextButton typeButton;
    juce::Slider volumeSlider;
    juce::Label busLabel;
    juce::Label effectsLabel;
    juce::Label soundLabel;
};
