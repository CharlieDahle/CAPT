#include "EnvelopeVisualizer.h"

void EnvelopeVisualizer::setEnvelope (EnvelopeParams newParams)
{
    params = newParams;
    repaint();
}

void EnvelopeVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.2f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    auto plotArea = bounds.reduced (6.0f);

    // Sustain has no duration of its own (it's a level, held for as long as
    // the key stays down) - a fixed stand-in width makes the plateau
    // visible without pretending to know how long a note will be held.
    constexpr float sustainHoldSeconds = 0.4f;
    auto totalSeconds = juce::jmax (0.05f, params.attackSeconds + params.decaySeconds
                                                + sustainHoldSeconds + params.releaseSeconds);

    auto attackWidth = plotArea.getWidth() * params.attackSeconds / totalSeconds;
    auto decayWidth = plotArea.getWidth() * params.decaySeconds / totalSeconds;
    auto sustainWidth = plotArea.getWidth() * sustainHoldSeconds / totalSeconds;

    auto x0 = plotArea.getX();
    auto yBottom = plotArea.getBottom();
    auto yTop = plotArea.getY();
    auto ySustain = juce::jmap (params.sustainLevel, 0.0f, 1.0f, yBottom, yTop);

    auto xAttackEnd = x0 + attackWidth;
    auto xDecayEnd = xAttackEnd + decayWidth;
    auto xSustainEnd = xDecayEnd + sustainWidth;
    auto xReleaseEnd = plotArea.getRight();

    juce::Path curve;
    curve.startNewSubPath (x0, yBottom);
    curve.lineTo (xAttackEnd, yTop);
    curve.lineTo (xDecayEnd, ySustain);
    curve.lineTo (xSustainEnd, ySustain);
    curve.lineTo (xReleaseEnd, yBottom);

    auto fill = curve;
    fill.lineTo (x0, yBottom);
    fill.closeSubPath();
    g.setColour (juce::Colours::limegreen.withAlpha (0.18f));
    g.fillPath (fill);

    // Same limegreen-on-black scheme as WaveformDisplay's scope, so the two
    // read as a matched pair of "audio views" rather than unrelated widgets.
    g.setColour (juce::Colours::limegreen);
    g.strokePath (curve, juce::PathStrokeType (2.0f));
}
