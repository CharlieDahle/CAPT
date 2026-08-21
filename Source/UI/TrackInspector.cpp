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

    quantizeButton.setButtonText ("Quantize");
    quantizeButton.onClick = [this]
    {
        if (currentTrack != nullptr && gridSettingsProvider != nullptr)
            currentTrack->quantize (gridSettingsProvider().stepsPerBeat);
    };
    addAndMakeVisible (quantizeButton);

    synthPanel.onWaveformChanged = [this] (OscillatorWaveform newWaveform)
    {
        if (currentTrack != nullptr)
            currentTrack->setWaveform (newWaveform);
    };
    synthPanel.onEnvelopeChanged = [this] (EnvelopeParams newParams)
    {
        if (currentTrack != nullptr)
            currentTrack->setEnvelope (newParams);
    };
    addAndMakeVisible (synthPanel);

    volumeLabel.setText ("Volume", juce::dontSendNotification);
    addAndMakeVisible (volumeLabel);

    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    volumeSlider.setRange (0.0, 1.0);
    volumeSlider.onValueChange = [this]
    {
        if (currentTrack != nullptr)
            currentTrack->setVolume ((float) volumeSlider.getValue());
    };
    addAndMakeVisible (volumeSlider);

    addAndMakeVisible (waveformDisplay);

    showTrack (nullptr);
}

void TrackInspector::showTrack (TrackBase* track)
{
    currentTrack = track;

    auto hasTrack = (track != nullptr);
    auto hasSynth = hasTrack && track->getType() == TrackType::Midi;
    armButton.setEnabled (hasTrack);
    typeButton.setEnabled (hasTrack);
    volumeSlider.setEnabled (hasTrack);
    quantizeButton.setEnabled (hasSynth);
    synthPanel.setControlsEnabled (hasSynth);

    if (! hasTrack)
    {
        headerLabel.setText ("No track selected", juce::dontSendNotification);
        return;
    }

    headerLabel.setText (track->getTrackName(), juce::dontSendNotification);
    armButton.setButtonText (track->isArmed() ? "Armed" : "Arm");
    typeButton.setButtonText (track->getType() == TrackType::Midi ? "MIDI" : "Audio");
    volumeSlider.setValue (track->getVolume(), juce::dontSendNotification);
    synthPanel.setWaveform (track->getWaveform());
    synthPanel.setEnvelope (track->getEnvelope());
}

void TrackInspector::resized()
{
    auto area = getLocalBounds().reduced (8);

    headerLabel.setBounds (area.removeFromTop (28));

    auto controlsArea = area.removeFromTop (32);
    juce::FlexBox controlsRow;
    controlsRow.flexDirection = juce::FlexBox::Direction::row;
    controlsRow.items.add (juce::FlexItem (armButton).withWidth (100).withMargin (4));
    controlsRow.items.add (juce::FlexItem (typeButton).withWidth (70).withMargin (4));
    controlsRow.items.add (juce::FlexItem (quantizeButton).withWidth (90).withMargin (4));
    controlsRow.performLayout (controlsArea);

    area.removeFromTop (4);

    auto volumeArea = area.removeFromTop (28);
    juce::FlexBox volumeRow;
    volumeRow.flexDirection = juce::FlexBox::Direction::row;
    volumeRow.items.add (juce::FlexItem (volumeLabel).withWidth (55).withMargin (4));
    volumeRow.items.add (juce::FlexItem (volumeSlider).withWidth (160).withMargin (4));
    volumeRow.performLayout (volumeArea);

    area.removeFromTop (4);

    // Scope on the left; the freed space to its right is the titled Synth
    // panel, so it reads as a defined section rather than blank canvas.
    waveformDisplay.setBounds (area.removeFromLeft (area.getWidth() * 3 / 5));
    area.removeFromLeft (8);
    synthPanel.setBounds (area);
}
