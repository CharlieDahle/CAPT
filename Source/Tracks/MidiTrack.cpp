#include "MidiTrack.h"

MidiTrack::MidiTrack (const juce::String& trackName)
    : TrackBase (trackName, TrackType::Midi)
{
    pianoRollView.setNotesProvider ([this] { return getRecordedNotesSnapshot(); });
    pianoRollView.setEditCallback ([this] (std::vector<PianoRollView::Note> notes) { setNotesFromEditor (notes); });
    pianoRollView.setEditableProvider ([this] { return lastGlobalState.load() == TransportState::Idle; });
    pianoRollView.setLaneClickHandler ([this] { selectOrToggleExpand(); });
    addAndMakeVisible (pianoRollView);

    for (int i = 0; i < 8; ++i)
        synth.addVoice (new SineWaveVoice());
    synth.addSound (new SineWaveSound());

    // Reserved so the push_back in renderNextBlock (audio thread, while
    // recording) never allocates - 128 covers every possible held pitch.
    heldRecordingNotes.reserve (128);
}

void MidiTrack::prepareToPlay (double sampleRate)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    currentSampleRate = sampleRate;
}

std::vector<PianoRollView::Note> MidiTrack::getRecordedNotesSnapshot() const
{
    auto count = numRecordedEvents.load();
    std::map<int, double> openNoteStartSeconds;
    std::vector<PianoRollView::Note> notes;

    for (int i = 0; i < count; ++i)
    {
        const auto& event = recordedEvents[(size_t) i];
        auto seconds = event.timeStampSamples / currentSampleRate;

        if (event.isNoteOn)
        {
            openNoteStartSeconds[event.noteNumber] = seconds;
            continue;
        }

        auto it = openNoteStartSeconds.find (event.noteNumber);
        if (it == openNoteStartSeconds.end())
            continue;

        notes.push_back ({ event.noteNumber, it->second, seconds, event.velocity });
        openNoteStartSeconds.erase (it);
    }

    return notes;
}

void MidiTrack::setNotesFromEditor (const std::vector<PianoRollView::Note>& notes)
{
    if (lastGlobalState.load() != TransportState::Idle)
        return;

    std::vector<RecordedNoteEvent> events;

    for (auto& note : notes)
    {
        events.push_back ({ note.startSeconds * currentSampleRate, note.pitch, note.velocity, true });
        events.push_back ({ note.endSeconds * currentSampleRate, note.pitch, note.velocity, false });
    }

    std::sort (events.begin(), events.end(),
               [] (const RecordedNoteEvent& a, const RecordedNoteEvent& b)
               { return a.timeStampSamples < b.timeStampSamples; });

    auto count = juce::jmin ((int) events.size(), maxRecordedEvents);

    for (int i = 0; i < count; ++i)
        recordedEvents[(size_t) i] = events[(size_t) i];

    numRecordedEvents.store (count);
}

void MidiTrack::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                  TransportState globalState, double elapsedSamples,
                                  const juce::AudioBuffer<const float>*)
{
    lastGlobalState.store (globalState);

    auto effectiveMode = globalState;

    if (globalState == TransportState::Recording && ! isArmed())
        effectiveMode = TransportState::Playing;

    if (effectiveMode != previousEffectiveMode)
    {
        // A note mid-hold when the mode changes never gets its matching
        // note-off from playback/recording logic, since that's about to
        // stop - tell the synth directly or the voice holds forever.
        synth.allNotesOff (1, false);

        // Recording stopped mid-note: the note-on made it into
        // recordedEvents but the note-off never will - close it out now.
        if (previousEffectiveMode == TransportState::Recording)
            closeOutHeldRecordingNotes (elapsedSamples);

        if (effectiveMode == TransportState::Playing)
            nextPlaybackIndex = 0;
        else if (effectiveMode == TransportState::Recording)
            numRecordedEvents = 0;

        previousEffectiveMode = effectiveMode;
    }

    juce::MidiBuffer liveMidi;
    keyboardState.processNextMidiBuffer (liveMidi, startSample, numSamples, true);

    juce::MidiBuffer midiForSynth;

    if (effectiveMode == TransportState::Playing)
    {
        while (nextPlaybackIndex < numRecordedEvents)
        {
            const auto& event = recordedEvents[(size_t) nextPlaybackIndex];
            auto samplesFromNow = event.timeStampSamples - elapsedSamples;

            if (samplesFromNow >= numSamples)
                break;

            auto samplePosition = juce::jlimit (0, numSamples - 1, (int) samplesFromNow);

            auto message = event.isNoteOn
                               ? juce::MidiMessage::noteOn (1, event.noteNumber, event.velocity)
                               : juce::MidiMessage::noteOff (1, event.noteNumber);

            midiForSynth.addEvent (message, samplePosition);
            ++nextPlaybackIndex;
        }
    }
    else
    {
        midiForSynth = liveMidi;
    }

    if (effectiveMode == TransportState::Recording)
    {
        for (const auto metadata : liveMidi)
        {
            if (numRecordedEvents >= maxRecordedEvents)
                break;

            auto message = metadata.getMessage();
            auto& slot = recordedEvents[(size_t) numRecordedEvents];

            slot.timeStampSamples = elapsedSamples + metadata.samplePosition;
            slot.noteNumber = message.getNoteNumber();
            slot.velocity = message.getFloatVelocity();
            slot.isNoteOn = message.isNoteOn();

            if (slot.isNoteOn)
                heldRecordingNotes.push_back (slot.noteNumber);
            else
                heldRecordingNotes.erase (std::remove (heldRecordingNotes.begin(), heldRecordingNotes.end(), slot.noteNumber),
                                           heldRecordingNotes.end());

            ++numRecordedEvents;
        }
    }

    synth.renderNextBlock (buffer, midiForSynth, startSample, numSamples);
}

void MidiTrack::injectTestNote (int noteNumber, float velocity, bool isNoteOn)
{
    if (isNoteOn)
        keyboardState.noteOn (1, noteNumber, velocity);
    else
        keyboardState.noteOff (1, noteNumber, velocity);
}

double MidiTrack::getLastEventTimeSamples() const
{
    auto count = numRecordedEvents.load();
    return count > 0 ? recordedEvents[(size_t) (count - 1)].timeStampSamples : 0.0;
}

std::unique_ptr<juce::XmlElement> MidiTrack::toXml (const juce::File&) const
{
    auto trackXml = std::make_unique<juce::XmlElement> ("MIDI_TRACK");
    writeVolumeAttribute (*trackXml);

    auto count = numRecordedEvents.load();

    for (int i = 0; i < count; ++i)
    {
        const auto& event = recordedEvents[(size_t) i];
        auto* eventXml = trackXml->createNewChildElement ("EVENT");
        eventXml->setAttribute ("time", event.timeStampSamples);
        eventXml->setAttribute ("note", event.noteNumber);
        eventXml->setAttribute ("velocity", (double) event.velocity);
        eventXml->setAttribute ("on", event.isNoteOn);
    }

    return trackXml;
}

void MidiTrack::fromXml (const juce::XmlElement& trackXml, const juce::File&)
{
    setVolumeFromXml (trackXml.getDoubleAttribute ("volume", 0.8));

    int count = 0;

    for (auto* eventXml : trackXml.getChildIterator())
    {
        if (count >= maxRecordedEvents)
            break;

        auto& slot = recordedEvents[(size_t) count];
        slot.timeStampSamples = eventXml->getDoubleAttribute ("time");
        slot.noteNumber = eventXml->getIntAttribute ("note");
        slot.velocity = (float) eventXml->getDoubleAttribute ("velocity");
        slot.isNoteOn = eventXml->getBoolAttribute ("on");
        ++count;
    }

    numRecordedEvents.store (count);
}

void MidiTrack::closeOutHeldRecordingNotes (double stopTimeSamples)
{
    for (auto note : heldRecordingNotes)
    {
        if (numRecordedEvents >= maxRecordedEvents)
            break;

        auto& slot = recordedEvents[(size_t) numRecordedEvents];
        slot.timeStampSamples = stopTimeSamples;
        slot.noteNumber = note;
        slot.velocity = 0.0f;
        slot.isNoteOn = false;
        ++numRecordedEvents;
    }

    heldRecordingNotes.clear();
}
