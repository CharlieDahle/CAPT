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

    // Copied into the visualizer directly (not just forwarded via
    // onEnvelopeChanged) so the shape preview updates immediately even if
    // no track is selected to receive the callback.
    auto notifyEnvelopeChanged = [this]
    {
        EnvelopeParams params { (float) attackSlider.getValue(), (float) decaySlider.getValue(),
                                 (float) sustainSlider.getValue(), (float) releaseSlider.getValue() };
        envelopeVisualizer.setEnvelope (params);

        if (onEnvelopeChanged)
            onEnvelopeChanged (params);
    };

    auto setUpEnvelopeKnob = [this, notifyEnvelopeChanged] (juce::Label& label, const juce::String& labelText,
                                                              juce::Slider& slider, double minValue, double maxValue,
                                                              double interval, const juce::String& suffix)
    {
        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (12.0f)));
        addAndMakeVisible (label);

        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
        slider.setRange (minValue, maxValue, interval);
        slider.setTextValueSuffix (suffix);
        slider.onValueChange = notifyEnvelopeChanged;
        addAndMakeVisible (slider);
    };

    // Attack/Decay/Release are stage durations - a couple of seconds covers
    // everything from a percussive pluck to a slow pad swell. Sustain is a
    // level, not a time, so it's 0-1 with no suffix.
    setUpEnvelopeKnob (attackLabel, "Attack", attackSlider, 0.0, 2.0, 0.001, " s");
    setUpEnvelopeKnob (decayLabel, "Decay", decaySlider, 0.0, 2.0, 0.001, " s");
    setUpEnvelopeKnob (sustainLabel, "Sustain", sustainSlider, 0.0, 1.0, 0.01, "");
    setUpEnvelopeKnob (releaseLabel, "Release", releaseSlider, 0.0, 3.0, 0.001, " s");

    // Not seeded from the knobs above (they default to 0, the bottom of
    // each range, until a track is selected) - EnvelopeVisualizer's own
    // default-constructed EnvelopeParams already matches MidiTrack's actual
    // starting envelope, so it starts correct without waiting for that.
    addAndMakeVisible (envelopeVisualizer);
}

void SynthControlsPanel::setWaveform (OscillatorWaveform waveform)
{
    waveformBox.setSelectedId ((int) waveform + 1, juce::dontSendNotification);
}

void SynthControlsPanel::setEnvelope (EnvelopeParams params)
{
    attackSlider.setValue (params.attackSeconds, juce::dontSendNotification);
    decaySlider.setValue (params.decaySeconds, juce::dontSendNotification);
    sustainSlider.setValue (params.sustainLevel, juce::dontSendNotification);
    releaseSlider.setValue (params.releaseSeconds, juce::dontSendNotification);

    // setValue above used dontSendNotification, so onValueChange (which
    // would otherwise keep this in sync) never fires - update it directly.
    envelopeVisualizer.setEnvelope (params);
}

void SynthControlsPanel::setControlsEnabled (bool shouldBeEnabled)
{
    waveformBox.setEnabled (shouldBeEnabled);
    attackSlider.setEnabled (shouldBeEnabled);
    decaySlider.setEnabled (shouldBeEnabled);
    sustainSlider.setEnabled (shouldBeEnabled);
    releaseSlider.setEnabled (shouldBeEnabled);
    envelopeVisualizer.setAlpha (shouldBeEnabled ? 1.0f : 0.4f);
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

    area.removeFromTop (14);

    // Knob diameter is whatever's left after the label (16) and the text
    // box JUCE reserves below the knob (16) - see setTextBoxStyle above.
    auto knobsArea = area.removeFromTop (90);
    auto knobWidth = knobsArea.getWidth() / 4;

    auto layoutKnob = [] (juce::Label& label, juce::Slider& slider, juce::Rectangle<int> column)
    {
        label.setBounds (column.removeFromTop (16));
        slider.setBounds (column);
    };

    layoutKnob (attackLabel, attackSlider, knobsArea.removeFromLeft (knobWidth));
    layoutKnob (decayLabel, decaySlider, knobsArea.removeFromLeft (knobWidth));
    layoutKnob (sustainLabel, sustainSlider, knobsArea.removeFromLeft (knobWidth));
    layoutKnob (releaseLabel, releaseSlider, knobsArea);

    area.removeFromTop (10);

    // Remaining `area` goes to the envelope shape preview - it grows along
    // with however much extra height the panel is given (see
    // MainComponent::resized), rather than a fixed size.
    envelopeVisualizer.setBounds (area);
}
