#include "SynthControlsPanel.h"

SynthControlsPanel::SynthControlsPanel()
{
    titleLabel.setText ("Synth", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    waveformLabel.setText ("Wave", juce::dontSendNotification);
    addAndMakeVisible (waveformLabel);

    waveformBox.addItem ("Sine", 1);
    waveformBox.addItem ("Saw", 2);
    waveformBox.addItem ("Square", 3);
    waveformBox.addItem ("Triangle", 4);
    waveformBox.onChange = [this]
    {
        if (onWaveformChanged)
            onWaveformChanged ((OscillatorWaveform) (waveformBox.getSelectedId() - 1));
    };
    addAndMakeVisible (waveformBox);
}

void SynthControlsPanel::setWaveform (OscillatorWaveform waveform)
{
    waveformBox.setSelectedId ((int) waveform + 1, juce::dontSendNotification);
}

void SynthControlsPanel::setControlsEnabled (bool shouldBeEnabled)
{
    waveformBox.setEnabled (shouldBeEnabled);
}

void SynthControlsPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void SynthControlsPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    titleLabel.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    auto waveformRow = area.removeFromTop (24);
    waveformLabel.setBounds (waveformRow.removeFromLeft (45));
    waveformBox.setBounds (waveformRow.removeFromLeft (120));

    // Remaining `area` is intentionally left empty - reserved for ADSR /
    // filter / effect knobs as they're added.
}
