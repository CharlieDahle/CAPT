#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Tracks/TrackBase.h"

// Dev/demo aid: plays a canned melody into a set of tracks by calling
// injectTestNote, so MIDI recording can be exercised without a MIDI
// keyboard.
class MelodyInjector : private juce::Timer
{
public:
    void start (std::vector<TrackBase*> targets);

    ~MelodyInjector() override;

private:
    struct Event { int timeMs; int note; bool isOn; };

    static std::vector<Event> buildMelody();

    void stopInFlightNotes();
    void timerCallback() override;

    std::vector<TrackBase*> targetTracks;
    std::vector<Event> events;
    std::vector<int> heldNotes;
    int nextEventIndex = 0;
    juce::uint32 startTimeMs = 0;
};
