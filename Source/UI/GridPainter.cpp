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

            // Beat (quarter-note) lines get the bar-line thickness even
            // though they keep the subdivision colour - a bar line is just
            // the beat line that also happens to start a bar. Anything
            // finer than a beat (e.g. the eighth-note line in between at
            // 1/8 grid density) stays thin.
            auto isBeatLine = grid.stepsPerBeat > 0 && (i % grid.stepsPerBeat) == 0;

            g.setColour (isBarLine ? barColour : subdivisionColour);
            g.drawLine (x, area.getY(), x, area.getBottom(), (isBarLine || isBeatLine) ? 2.0f : 1.0f);
        }
    }
}
