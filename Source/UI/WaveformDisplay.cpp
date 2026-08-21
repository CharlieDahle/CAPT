#include "WaveformDisplay.h"

WaveformDisplay::WaveformDisplay()
{
    // History length is tuned against pixel width, not just time: this scope
    // only occupies the left ~3/5 of the inspector panel now (room was made
    // for the Synth panel beside it), so the same history that looked right
    // at full width gets packed into fewer pixels and looks squished. Scaled
    // down by roughly that same 3/5 factor to keep cycles the same size on
    // screen as before - ~56ms of history now instead of ~93ms.
    scope.setBufferSize (1024);
    scope.setSamplesPerBlock (2);
    scope.setColours (juce::Colours::black, juce::Colours::limegreen);
    scope.setRepaintRate (30);
    addAndMakeVisible (scope);
}
