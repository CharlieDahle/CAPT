#include "TrackBase.h"

TrackBase::TrackBase (const juce::String& trackName, TrackType type)
    : trackType (type)
{
    nameLabel.setText (trackName, juce::dontSendNotification);
    nameLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (nameLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.onClick = [this] { muted.store (muteButton.getToggleState()); };
    addAndMakeVisible (muteButton);

    soloButton.setClickingTogglesState (true);
    soloButton.onClick = [this] { soloed.store (soloButton.getToggleState()); };
    addAndMakeVisible (soloButton);

    selectButton.setClickingTogglesState (false);
    selectButton.onClick = [this] { if (onSelected) onSelected(); };
    addAndMakeVisible (selectButton);
}

void TrackBase::setSelected (bool newSelected)
{
    selected = newSelected;
    selectButton.setToggleState (selected, juce::dontSendNotification);
    selectionChanged (selected);
    repaint();
}

void TrackBase::setExpanded (bool newExpanded)
{
    expanded = newExpanded;
    muteButton.setVisible (! expanded);
    soloButton.setVisible (! expanded);
    selectButton.setVisible (! expanded);
    expandedChanged (expanded);
    repaint();
}

void TrackBase::paint (juce::Graphics& g)
{
    if (selected)
    {
        g.fillAll (juce::Colours::white.withAlpha (0.12f));
        g.setColour (juce::Colours::white);
        g.drawRect (getLocalBounds());
    }

    // Same reserved gutter as the piano roll below - keeps the left
    // margin reading as space outside the track's box, not part of it.
    g.setColour (juce::Colours::darkslategrey);
    g.fillRect (0, 0, (int) kKeyboardStripWidth, 20);
}

void TrackBase::mouseDown (const juce::MouseEvent&)
{
    if (onSelected)
        onSelected();
}

void TrackBase::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onExpandToggleRequested)
        onExpandToggleRequested();
}

void TrackBase::resized()
{
    auto area = getLocalBounds();
    auto headerArea = area.removeFromTop (20);
    headerArea.removeFromLeft ((int) kKeyboardStripWidth);
    nameLabel.setBounds (headerArea);
    resizedContent (area);

    // Overlaid on the same gutter the content below draws into, so
    // bring to front regardless of add order.
    auto buttonColumn = area.removeFromLeft ((int) kKeyboardStripWidth);
    auto buttonSize = juce::jmin (buttonColumn.getWidth(), buttonColumn.getHeight() / 3);
    muteButton.setBounds (buttonColumn.removeFromTop (buttonSize));
    soloButton.setBounds (buttonColumn.removeFromTop (buttonSize));
    selectButton.setBounds (buttonColumn.removeFromTop (buttonSize));
    muteButton.toFront (false);
    soloButton.toFront (false);
    selectButton.toFront (false);
}

void TrackBase::selectOrToggleExpand()
{
    if (! selected)
    {
        if (onSelected)
            onSelected();
    }
    else if (onExpandToggleRequested)
    {
        onExpandToggleRequested();
    }
}
