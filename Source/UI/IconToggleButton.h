#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// A small round toggle button drawn as a filled/outline circle with an
// optional single-character label - used for the per-track mute/solo/select
// buttons that live in the piano-key gutter.
class IconToggleButton : public juce::Button
{
public:
    IconToggleButton (juce::String iconText, juce::Colour activeColourToUse)
        : juce::Button ({}), text (std::move (iconText)), activeColour (activeColourToUse)
    {
    }

    void paintButton (juce::Graphics& g, bool isHighlighted, bool /*isDown*/) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (3.0f);
        auto active = getToggleState();

        g.setColour (active ? activeColour : juce::Colours::white.withAlpha (isHighlighted ? 0.2f : 0.08f));
        g.fillEllipse (bounds);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawEllipse (bounds, 1.0f);

        if (text.isNotEmpty())
        {
            g.setColour (active ? juce::Colours::black : juce::Colours::white.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText (text, getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    juce::String text;
    juce::Colour activeColour;
};
