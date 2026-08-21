#include "MelodyInjector.h"

void MelodyInjector::start (std::vector<TrackBase*> targets)
{
    stopInFlightNotes();

    targetTracks = std::move (targets);
    events = buildMelody();
    nextEventIndex = 0;
    startTimeMs = juce::Time::getMillisecondCounter();
    startTimerHz (50);
}

MelodyInjector::~MelodyInjector()
{
    stopTimer();
    stopInFlightNotes();
}

std::vector<MelodyInjector::Event> MelodyInjector::buildMelody()
{
    static const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    std::vector<Event> result;
    int t = 0;

    for (auto note : scale)
    {
        result.push_back ({ t, note, true });
        result.push_back ({ t + 250, note, false });
        t += 300;
    }

    return result;
}

void MelodyInjector::stopInFlightNotes()
{
    for (auto note : heldNotes)
        for (auto* track : targetTracks)
            track->injectTestNote (note, 0.8f, false);

    heldNotes.clear();
}

void MelodyInjector::timerCallback()
{
    auto elapsedMs = (int) (juce::Time::getMillisecondCounter() - startTimeMs);

    while (nextEventIndex < (int) events.size() && events[(size_t) nextEventIndex].timeMs <= elapsedMs)
    {
        const auto& event = events[(size_t) nextEventIndex];

        if (event.isOn)
            heldNotes.push_back (event.note);
        else
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), event.note), heldNotes.end());

        for (auto* track : targetTracks)
            track->injectTestNote (event.note, 0.8f, event.isOn);

        ++nextEventIndex;
    }

    if (nextEventIndex >= (int) events.size())
        stopTimer();
}
