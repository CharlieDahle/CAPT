#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Tempo.h"

// A thin strip above the track lanes showing bar numbers, sharing the same
// x-mapping (kKeyboardStripWidth + beats*kPixelsPerBeat) the playhead and
// piano-roll grid use, so everything lines up.
class TimelineRuler : public juce::Component
{
public:
    void setGridSettingsProvider (std::function<GridSettings()> provider) { gridSettingsProvider = std::move (provider); }

    void paint (juce::Graphics& g) override;

private:
    std::function<GridSettings()> gridSettingsProvider;
};
