#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Tracks/TrackBase.h"

// Lays out the track lanes stacked vertically, giving the expanded one (if
// any) extra height. Content component inside a Viewport, so its total
// height can exceed what's actually visible.
class TracksContainer : public juce::Component
{
public:
    static constexpr int collapsedHeight = 110;
    static constexpr int expandedHeight = 320;

    void attachTracks (std::vector<std::unique_ptr<TrackBase>>* tracksToUse) { tracksPtr = tracksToUse; }

    // Swapping which track is expanded leaves the total height unchanged,
    // so the parent's setSize() call afterwards won't detect a bounds
    // change and won't fire resized() for us - lay out directly here.
    void setExpandedIndex (int index)
    {
        expandedIndex = index;
        resized();
    }

    int computeTotalHeight() const;

    void resized() override;

private:
    std::vector<std::unique_ptr<TrackBase>>* tracksPtr = nullptr;
    int expandedIndex = -1;
};
