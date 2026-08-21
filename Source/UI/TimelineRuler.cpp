#include "TimelineRuler.h"
#include "../Core/Types.h"

void TimelineRuler::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.25f));

    if (gridSettingsProvider == nullptr)
        return;

    auto grid = gridSettingsProvider();
    auto beatsPerBar = (double) grid.timeSignature.numerator;

    if (beatsPerBar <= 0.0)
        return;

    auto maxBeats = (double) ((float) getWidth() - kKeyboardStripWidth) / kPixelsPerBeat;

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int bar = 0; beatsPerBar * (double) bar <= maxBeats; ++bar)
    {
        auto x = kKeyboardStripWidth + (float) (beatsPerBar * (double) bar * kPixelsPerBeat);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());
        g.drawText (juce::String (bar + 1), (int) x + 2, 0, 60, getHeight(), juce::Justification::centredLeft);
    }
}
