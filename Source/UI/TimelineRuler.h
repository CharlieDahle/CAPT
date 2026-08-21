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

    // Click or click-drag anywhere in the ruler to move the playhead there,
    // snapped to the current grid - fired with the target position in
    // beats, letting the caller own the beats<->samples/tempo conversion
    // (this component otherwise has no reason to know about either).
    std::function<void (double)> onSeekRequested;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override { seekToX (e.position.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekToX (e.position.x); }

private:
    void seekToX (float x);

    std::function<GridSettings()> gridSettingsProvider;
};
