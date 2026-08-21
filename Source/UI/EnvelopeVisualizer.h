#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"

// Schematic ADSR curve, not a rendering of real audio (that's WaveformDisplay's
// job) - just a shape diagram that redraws whenever the envelope parameters
// change, so turning a knob is immediately visible even with no note playing.
// Stage widths are proportional to their actual attack/decay/release values
// (plus a fixed stand-in width for the sustain hold, since sustain is a
// level with no duration of its own), so a longer attack visibly widens
// that segment relative to the others instead of every knob just moving an
// unrelated number.
class EnvelopeVisualizer : public juce::Component
{
public:
    void setEnvelope (EnvelopeParams newParams);

    void paint (juce::Graphics& g) override;

private:
    EnvelopeParams params;
};
