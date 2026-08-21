#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Tempo.h"

// Shared vertical bar/beat grid-line painter, used by both the piano roll
// editor and MainComponent's cross-track overlay so the two always agree
// pixel-for-pixel on where the grid falls.
namespace GridPainter
{
    void paintVerticalGridLines (juce::Graphics& g, juce::Rectangle<float> area, float xOrigin,
                                  const GridSettings& grid, juce::Colour barColour, juce::Colour subdivisionColour);
}
