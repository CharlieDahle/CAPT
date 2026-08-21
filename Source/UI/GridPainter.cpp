#include "GridPainter.h"
#include "../Core/Types.h"
#include <cmath>

namespace GridPainter
{
    void paintVerticalGridLines (juce::Graphics& g, juce::Rectangle<float> area, float xOrigin,
                                  const GridSettings& grid, juce::Colour barColour, juce::Colour subdivisionColour)
    {
        auto step = 1.0 / (double) juce::jmax (1, grid.stepsPerBeat);
        auto maxBeats = (double) (area.getRight() - xOrigin) / kPixelsPerBeat;
        auto stepsPerBar = (double) grid.stepsPerBeat * (double) grid.timeSignature.numerator;

        for (int i = 0; step * (double) i <= maxBeats; ++i)
        {
            auto x = xOrigin + (float) (step * (double) i * kPixelsPerBeat);
            auto isBarLine = stepsPerBar > 0.0 && std::fmod ((double) i, stepsPerBar) < 0.5;

            g.setColour (isBarLine ? barColour : subdivisionColour);
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
    }
}
