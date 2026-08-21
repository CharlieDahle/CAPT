#include "TrackInspector.h"

TrackInspector::TrackInspector()
{
    headerLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    addAndMakeVisible (headerLabel);

    armButton.setButtonText ("Arm");
    armButton.onClick = [this]
    {
        if (currentTrack == nullptr)
            return;

        auto newArmed = ! currentTrack->isArmed();
        currentTrack->setArmed (newArmed);
        armButton.setButtonText (newArmed ? "Armed" : "Arm");
    };
    addAndMakeVisible (armButton);

    typeButton.setButtonText ("-");
    typeButton.onClick = [this]
    {
        if (currentTrack != nullptr && currentTrack->onTypeToggleRequested)
            currentTrack->onTypeToggleRequested();
    };
    addAndMakeVisible (typeButton);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    volumeSlider.setRange (0.0, 1.0);
    volumeSlider.onValueChange = [this]
    {
        if (currentTrack != nullptr)
            currentTrack->setVolume ((float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    busLabel.setText ("Bus: (not yet implemented)", juce::dontSendNotification);
    addAndMakeVisible (busLabel);

    effectsLabel.setText ("Effects: (not yet implemented)", juce::dontSendNotification);
    addAndMakeVisible (effectsLabel);

    soundLabel.setText ("Sound: (not yet implemented)", juce::dontSendNotification);
    addAndMakeVisible (soundLabel);

    showTrack (nullptr);
}

void TrackInspector::showTrack (TrackBase* track)
{
    currentTrack = track;

    auto hasTrack = (track != nullptr);
    armButton.setEnabled (hasTrack);
    typeButton.setEnabled (hasTrack);
    volumeSlider.setEnabled (hasTrack);

    if (! hasTrack)
    {
        headerLabel.setText ("No track selected", juce::dontSendNotification);
        return;
    }

    headerLabel.setText (track->getTrackName(), juce::dontSendNotification);
    armButton.setButtonText (track->isArmed() ? "Armed" : "Arm");
    typeButton.setButtonText (track->getType() == TrackType::Midi ? "MIDI" : "Audio");
    volumeSlider.setValue (track->getVolume(), juce::dontSendNotification);
}

void TrackInspector::resized()
{
    auto area = getLocalBounds().reduced (8);

    headerLabel.setBounds (area.removeFromTop (28));

    auto controlsArea = area.removeFromTop (36);
    juce::FlexBox controlsRow;
    controlsRow.flexDirection = juce::FlexBox::Direction::row;
    controlsRow.items.add (juce::FlexItem (armButton).withWidth (100).withMargin (4));
    controlsRow.items.add (juce::FlexItem (typeButton).withWidth (70).withMargin (4));
    controlsRow.items.add (juce::FlexItem (volumeSlider).withFlex (1).withMargin (4));
    controlsRow.performLayout (controlsArea);

    area.removeFromTop (12);

    juce::FlexBox placeholderColumn;
    placeholderColumn.flexDirection = juce::FlexBox::Direction::column;
    placeholderColumn.items.add (juce::FlexItem (busLabel).withHeight (24));
    placeholderColumn.items.add (juce::FlexItem (effectsLabel).withHeight (24));
    placeholderColumn.items.add (juce::FlexItem (soundLabel).withHeight (24));
    placeholderColumn.performLayout (area);
}
