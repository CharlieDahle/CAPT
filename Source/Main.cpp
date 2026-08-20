#include <juce_audio_utils/juce_audio_utils.h>
#include "MicrophonePermission.h"
#include <map>

struct SineWaveSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override         { return true; }
    bool appliesToChannel (int) override       { return true; }
};

struct SineWaveVoice : public juce::SynthesiserVoice
{
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        phase = 0.0;
        auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        phaseDelta = frequency / getSampleRate();
        level = velocity * 0.2f;
        active = true;
    }

    void stopNote (float /*velocity*/, bool /*allowTailOff*/) override
    {
        active = false;
        clearCurrentNote();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                           int startSample, int numSamples) override
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sampleValue = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase) * level;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, startSample + i, sampleValue);

            phase += phaseDelta;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    bool active = false;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float level = 0.0f;
};

enum class TransportState { Idle, Recording, Playing };
enum class TrackType { Midi, Audio };

// Shared horizontal timeline scale for the piano roll and the playhead, so
// a note drawn at a given x lines up with the playhead when it gets there.
constexpr double kPixelsPerSecond = 60.0;

struct RecordedNoteEvent
{
    double timeStampSamples = 0.0;
    int noteNumber = 0;
    float velocity = 0.0f;
    bool isNoteOn = false;
};

class TrackBase : public juce::Component
{
public:
    TrackBase (const juce::String& trackName, TrackType type)
        : trackType (type)
    {
        nameLabel.setText (trackName, juce::dontSendNotification);
        // Let clicks on the name pass through to this component's own
        // mouseDown() so clicking the "Track X" text selects the track.
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);
    }

    ~TrackBase() override = default;

    virtual void prepareToPlay (double sampleRate) = 0;

    // inputBuffer holds this block's raw microphone input (nullptr if no
    // input device is available); only AudioTrack makes use of it.
    virtual void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                   TransportState globalState, double elapsedSamples,
                                   const juce::AudioBuffer<const float>* inputBuffer) = 0;

    virtual double getLastEventTimeSamples() const = 0;

    // audioFolder is where any recorded audio should be written/read from
    // (e.g. "<project>_Audio/"); tracks with nothing to persist there just
    // ignore it.
    virtual std::unique_ptr<juce::XmlElement> toXml (const juce::File& audioFolder) const = 0;
    virtual void fromXml (const juce::XmlElement& trackXml, const juce::File& audioFolder) = 0;

    // Only meaningful for tracks that accept live MIDI input; other track
    // types just ignore simulated notes.
    virtual void injectTestNote (int /*noteNumber*/, float /*velocity*/, bool /*isNoteOn*/) {}

    float getVolume() const { return volume.load(); }
    void setVolume (float newVolume) { volume.store (newVolume); }
    bool isArmed() const { return armed.load(); }
    void setArmed (bool newArmed) { armed.store (newArmed); }
    TrackType getType() const { return trackType; }
    juce::String getTrackName() const { return nameLabel.getText(); }

    void setSelected (bool newSelected)
    {
        selected = newSelected;
        repaint();
    }

    // Set by whoever owns this track (the slot it lives in decides how to
    // react - e.g. by replacing it with a track of the other type).
    std::function<void()> onTypeToggleRequested;

    // Fired when the lane is clicked, so the owner can make this the
    // selected track (and show it in the inspector panel below).
    std::function<void()> onSelected;

    void paint (juce::Graphics& g) override
    {
        if (selected)
        {
            g.fillAll (juce::Colours::white.withAlpha (0.12f));
            g.setColour (juce::Colours::white);
            g.drawRect (getLocalBounds());
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onSelected)
            onSelected();
    }

    void resized() final
    {
        auto area = getLocalBounds();
        nameLabel.setBounds (area.removeFromTop (20));
        resizedContent (area);
    }

protected:
    // Subclasses lay out their own content (keyboard, waveform, ...) in the
    // area below the name label.
    virtual void resizedContent (juce::Rectangle<int> contentArea) = 0;

    void setVolumeFromXml (double newVolume)
    {
        volume.store ((float) newVolume);
    }

    void writeVolumeAttribute (juce::XmlElement& trackXml) const
    {
        trackXml.setAttribute ("volume", (double) volume.load());
    }

private:
    TrackType trackType;

    juce::Label nameLabel;

    std::atomic<float> volume { 0.8f };
    std::atomic<bool> armed { false };
    bool selected = false;
};

// Draws a track's recorded MIDI notes as horizontal bars (pitch on the
// y-axis, time on the x-axis) and lets them be drawn/moved/resized/deleted
// with the mouse - but only while editableProvider says the transport is
// idle. That restriction is what makes this safe without any locking: the
// owning MidiTrack only touches its note storage from the audio thread
// while actively recording/playing, and this view only mutates it (via
// commit()) while that's guaranteed not to be happening.
class PianoRollView : public juce::Component, private juce::Timer
{
public:
    struct Note
    {
        int pitch = 60;
        double startSeconds = 0.0;
        double endSeconds = 0.5;
        float velocity = 0.8f;
    };

    PianoRollView()
    {
        startTimerHz (15);
    }

    void setNotesProvider (std::function<std::vector<Note>()> provider) { notesProvider = std::move (provider); }
    void setEditCallback (std::function<void (std::vector<Note>)> callback) { editCallback = std::move (callback); }
    void setEditableProvider (std::function<bool()> provider) { editableProvider = std::move (provider); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        if (! isEditable())
        {
            g.setColour (juce::Colours::darkred.withAlpha (0.25f));
            g.fillAll();
        }

        for (auto& note : notes)
        {
            g.setColour (juce::Colours::limegreen.withAlpha (juce::jlimit (0.3f, 1.0f, note.velocity)));
            g.fillRect (noteBounds (note));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEditable())
            return;

        if (e.mods.isRightButtonDown())
        {
            auto hitIndex = findNoteAt (e.position);

            if (hitIndex >= 0)
            {
                notes.erase (notes.begin() + hitIndex);
                commit();
            }

            return;
        }

        auto hitIndex = findNoteAt (e.position);

        if (hitIndex >= 0)
        {
            draggingIndex = hitIndex;
            dragStartNote = notes[(size_t) hitIndex];
            dragMode = isNearRightEdge (e.position, notes[(size_t) hitIndex]) ? DragMode::resize : DragMode::move;
        }
        else
        {
            Note newNote;
            newNote.pitch = yToPitch (e.position.y);
            newNote.startSeconds = juce::jmax (0.0, xToSeconds (e.position.x));
            newNote.endSeconds = newNote.startSeconds + 0.5;
            notes.push_back (newNote);
            draggingIndex = (int) notes.size() - 1;
            dragStartNote = newNote;
            dragMode = DragMode::resize;
        }

        dragStartPos = e.position;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingIndex < 0)
            return;

        auto& note = notes[(size_t) draggingIndex];
        auto deltaSeconds = (e.position.x - dragStartPos.x) / kPixelsPerSecond;

        if (dragMode == DragMode::move)
        {
            auto length = dragStartNote.endSeconds - dragStartNote.startSeconds;
            note.startSeconds = juce::jmax (0.0, dragStartNote.startSeconds + deltaSeconds);
            note.endSeconds = note.startSeconds + length;
            note.pitch = yToPitch (e.position.y);
        }
        else
        {
            note.endSeconds = juce::jmax (note.startSeconds + 0.05, dragStartNote.endSeconds + deltaSeconds);
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (draggingIndex < 0)
            return;

        draggingIndex = -1;
        commit();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! isEditable())
            return;

        auto hitIndex = findNoteAt (e.position);
        if (hitIndex < 0)
            return;

        notes[(size_t) hitIndex].velocity = juce::jlimit (0.05f, 1.0f, notes[(size_t) hitIndex].velocity + wheel.deltaY * 0.5f);
        commit();
    }

private:
    enum class DragMode { move, resize };

    bool isEditable() const { return editableProvider != nullptr && editableProvider(); }

    juce::Rectangle<float> noteBounds (const Note& note) const
    {
        auto x = (float) (note.startSeconds * kPixelsPerSecond);
        auto width = juce::jmax (2.0f, (float) ((note.endSeconds - note.startSeconds) * kPixelsPerSecond));
        return { x, pitchToY (note.pitch), width, noteHeight };
    }

    bool isNearRightEdge (juce::Point<float> position, const Note& note) const
    {
        auto bounds = noteBounds (note);
        return std::abs (position.x - bounds.getRight()) < 6.0f && bounds.getY() - 4.0f <= position.y
               && position.y <= bounds.getBottom() + 4.0f;
    }

    int findNoteAt (juce::Point<float> position) const
    {
        for (int i = (int) notes.size() - 1; i >= 0; --i)
            if (noteBounds (notes[(size_t) i]).expanded (0.0f, 3.0f).contains (position))
                return i;

        return -1;
    }

    float pitchToY (int pitch) const
    {
        auto clamped = juce::jlimit (lowestNote, highestNote, pitch);
        auto fraction = (double) (highestNote - clamped) / (double) (highestNote - lowestNote);
        return (float) (fraction * juce::jmax (0, getHeight() - (int) noteHeight));
    }

    int yToPitch (float y) const
    {
        auto fraction = juce::jlimit (0.0f, 1.0f, y / (float) juce::jmax (1, getHeight()));
        return highestNote - juce::roundToInt (fraction * (highestNote - lowestNote));
    }

    double xToSeconds (float x) const { return x / kPixelsPerSecond; }

    void commit()
    {
        if (editCallback != nullptr)
            editCallback (notes);

        repaint();
    }

    void timerCallback() override
    {
        // Don't clobber in-progress edits with a stale snapshot from the
        // track - editableProvider already guarantees the two never
        // overlap in practice (edits are only possible while idle, and
        // the track only rewrites its own data while idle), but this
        // keeps the view stable mid-drag regardless.
        if (draggingIndex < 0 && notesProvider != nullptr)
            notes = notesProvider();

        repaint();
    }

    std::function<std::vector<Note>()> notesProvider;
    std::function<void (std::vector<Note>)> editCallback;
    std::function<bool()> editableProvider;

    std::vector<Note> notes;
    int draggingIndex = -1;
    DragMode dragMode = DragMode::move;
    Note dragStartNote;
    juce::Point<float> dragStartPos;

    static constexpr int lowestNote = 36;
    static constexpr int highestNote = 96;
    static constexpr float noteHeight = 4.0f;
};

class MidiTrack : public TrackBase
{
public:
    explicit MidiTrack (const juce::String& trackName)
        : TrackBase (trackName, TrackType::Midi)
    {
        pianoRollView.setNotesProvider ([this] { return getRecordedNotesSnapshot(); });
        pianoRollView.setEditCallback ([this] (std::vector<PianoRollView::Note> notes) { setNotesFromEditor (notes); });
        pianoRollView.setEditableProvider ([this] { return lastGlobalState.load() == TransportState::Idle; });
        addAndMakeVisible (pianoRollView);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new SineWaveVoice());
        synth.addSound (new SineWaveSound());
    }

    void prepareToPlay (double sampleRate) override
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
        currentSampleRate = sampleRate;
    }

    // Pairs the raw on/off event log into editor-friendly notes. Safe to
    // call from the message thread: while the transport is idle (the only
    // time the editor reads this) the audio thread never touches
    // recordedEvents/numRecordedEvents.
    std::vector<PianoRollView::Note> getRecordedNotesSnapshot() const
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

    // Rewrites recordedEvents from a fully-edited note list. Only called
    // (via the editor) while the transport is idle - see the class comment
    // on PianoRollView for why that makes this safe without locking.
    void setNotesFromEditor (const std::vector<PianoRollView::Note>& notes)
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

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           TransportState globalState, double elapsedSamples,
                           const juce::AudioBuffer<const float>*) override
    {
        lastGlobalState.store (globalState);

        auto effectiveMode = globalState;

        if (globalState == TransportState::Recording && ! isArmed())
            effectiveMode = TransportState::Playing;

        if (effectiveMode != previousEffectiveMode)
        {
            // Any note that was mid-hold when the mode changed (transport
            // stopped, or arm toggled mid-recording) never gets its
            // matching note-off, since that came from playback/recording
            // logic that's about to stop running - so tell the synth
            // directly, or the voice would hold forever.
            synth.allNotesOff (1, false);

            // If recording just stopped mid-note, the note-on already made
            // it into recordedEvents but its note-off never will - close it
            // out now so future playback doesn't hold the note forever.
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

    void injectTestNote (int noteNumber, float velocity, bool isNoteOn) override
    {
        if (isNoteOn)
            keyboardState.noteOn (1, noteNumber, velocity);
        else
            keyboardState.noteOff (1, noteNumber, velocity);
    }

    double getLastEventTimeSamples() const override
    {
        auto count = numRecordedEvents.load();
        return count > 0 ? recordedEvents[(size_t) (count - 1)].timeStampSamples : 0.0;
    }

    std::unique_ptr<juce::XmlElement> toXml (const juce::File&) const override
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

    void fromXml (const juce::XmlElement& trackXml, const juce::File&) override
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

protected:
    void resizedContent (juce::Rectangle<int> contentArea) override
    {
        pianoRollView.setBounds (contentArea);
    }

private:
    void closeOutHeldRecordingNotes (double stopTimeSamples)
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

    juce::MidiKeyboardState keyboardState;
    PianoRollView pianoRollView;
    juce::Synthesiser synth;
    double currentSampleRate = 44100.0;
    std::atomic<TransportState> lastGlobalState { TransportState::Idle };

    int nextPlaybackIndex = 0;
    TransportState previousEffectiveMode = TransportState::Idle;

    static constexpr int maxRecordedEvents = 4096;
    std::array<RecordedNoteEvent, maxRecordedEvents> recordedEvents;
    std::atomic<int> numRecordedEvents { 0 };
    std::vector<int> heldRecordingNotes;
};

// Simple horizontal bar that repaints itself from a live level value -
// lets you see whether input is actually reaching a track's mic capture,
// independent of whether you're recording.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (const std::atomic<float>& levelToWatch)
        : level (levelToWatch)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        auto levelValue = juce::jlimit (0.0f, 1.0f, level.load());
        auto barWidth = (int) (getWidth() * levelValue);

        g.setColour (juce::Colours::limegreen);
        g.fillRect (0, 0, barWidth, getHeight());
    }

private:
    void timerCallback() override { repaint(); }

    const std::atomic<float>& level;
};

// Records mono microphone input into an in-memory buffer and plays it back.
// No waveform view yet (placeholder label stands in for it), and recordings
// aren't persisted to disk yet - toXml()/fromXml() only round-trip volume.
class AudioTrack : public TrackBase
{
public:
    explicit AudioTrack (const juce::String& trackName)
        : TrackBase (trackName, TrackType::Audio)
    {
        placeholderLabel.setText ("Audio track (waveform view coming soon)", juce::dontSendNotification);
        placeholderLabel.setJustificationType (juce::Justification::centred);
        placeholderLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (placeholderLabel);

        levelMeter.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (levelMeter);
    }

    void prepareToPlay (double sampleRate) override
    {
        currentSampleRate = sampleRate;
    }

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           TransportState globalState, double elapsedSamples,
                           const juce::AudioBuffer<const float>* inputBuffer) override
    {
        // Live input level, independent of transport state - lets you see
        // whether the mic is picking anything up before/without recording.
        if (isArmed() && inputBuffer != nullptr && inputBuffer->getNumChannels() > 0)
        {
            auto* inputData = inputBuffer->getReadPointer (0, startSample);
            float peak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (inputData[i]));

            currentLevel.store (juce::jmax (peak, currentLevel.load() * 0.85f));
        }
        else
        {
            currentLevel.store (currentLevel.load() * 0.85f);
        }

        auto effectiveMode = globalState;

        if (globalState == TransportState::Recording && ! isArmed())
            effectiveMode = TransportState::Playing;

        if (effectiveMode != previousEffectiveMode)
        {
            if (effectiveMode == TransportState::Recording)
                recordedSamples.clear();

            previousEffectiveMode = effectiveMode;
        }

        if (effectiveMode == TransportState::Recording && inputBuffer != nullptr && inputBuffer->getNumChannels() > 0)
        {
            auto* inputData = inputBuffer->getReadPointer (0, startSample);
            recordedSamples.insert (recordedSamples.end(), inputData, inputData + numSamples);
        }
        else if (effectiveMode == TransportState::Playing)
        {
            auto playbackIndex = (size_t) elapsedSamples;

            for (int i = 0; i < numSamples; ++i)
            {
                auto sampleIndex = playbackIndex + (size_t) i;

                if (sampleIndex >= recordedSamples.size())
                    break;

                auto sampleValue = recordedSamples[sampleIndex];

                for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                    buffer.addSample (channel, startSample + i, sampleValue);
            }
        }
    }

    double getLastEventTimeSamples() const override
    {
        return (double) recordedSamples.size();
    }

    std::unique_ptr<juce::XmlElement> toXml (const juce::File& audioFolder) const override
    {
        auto trackXml = std::make_unique<juce::XmlElement> ("AUDIO_TRACK");
        writeVolumeAttribute (*trackXml);

        if (! recordedSamples.empty())
        {
            auto fileName = getTrackName() + ".wav";
            audioFolder.createDirectory();
            writeWavFile (audioFolder.getChildFile (fileName));
            trackXml->setAttribute ("audioFile", fileName);
        }

        return trackXml;
    }

    void fromXml (const juce::XmlElement& trackXml, const juce::File& audioFolder) override
    {
        setVolumeFromXml (trackXml.getDoubleAttribute ("volume", 0.8));

        auto fileName = trackXml.getStringAttribute ("audioFile");

        if (fileName.isNotEmpty())
            readWavFile (audioFolder.getChildFile (fileName));
        else
            recordedSamples.clear();
    }

protected:
    void resizedContent (juce::Rectangle<int> contentArea) override
    {
        juce::FlexBox column;
        column.flexDirection = juce::FlexBox::Direction::column;
        column.items.add (juce::FlexItem (levelMeter).withHeight (20).withMargin (2));
        column.items.add (juce::FlexItem (placeholderLabel).withFlex (1));
        column.performLayout (contentArea);
    }

private:
    void writeWavFile (const juce::File& file) const
    {
        file.deleteFile();

        std::unique_ptr<juce::OutputStream> outputStream (new juce::FileOutputStream (file));

        auto options = juce::AudioFormatWriterOptions()
                           .withSampleRate (currentSampleRate)
                           .withNumChannels (1)
                           .withBitsPerSample (16);

        juce::WavAudioFormat wavFormat;
        auto writer = wavFormat.createWriterFor (outputStream, options);

        if (writer == nullptr)
            return;

        auto* channelPtr = recordedSamples.data();
        writer->writeFromFloatArrays (&channelPtr, 1, (int) recordedSamples.size());
    }

    void readWavFile (const juce::File& file)
    {
        recordedSamples.clear();

        if (! file.existsAsFile())
            return;

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatReader> reader (
            wavFormat.createReaderFor (new juce::FileInputStream (file), true));

        if (reader == nullptr)
            return;

        recordedSamples.resize ((size_t) reader->lengthInSamples);
        auto* destPtr = recordedSamples.data();
        reader->read (&destPtr, 1, 0, (int) reader->lengthInSamples);
    }

    juce::Label placeholderLabel;
    std::atomic<float> currentLevel { 0.0f };
    LevelMeter levelMeter { currentLevel };
    std::vector<float> recordedSamples;
    TransportState previousEffectiveMode = TransportState::Idle;
    double currentSampleRate = 44100.0;
};

static std::unique_ptr<TrackBase> makeTrack (TrackType type, const juce::String& trackName)
{
    if (type == TrackType::Audio)
        return std::make_unique<AudioTrack> (trackName);

    return std::make_unique<MidiTrack> (trackName);
}

static TrackType trackTypeForXmlTag (const juce::String& tagName)
{
    return tagName == "AUDIO_TRACK" ? TrackType::Audio : TrackType::Midi;
}

class MelodyInjector : private juce::Timer
{
public:
    void start (std::vector<TrackBase*> targets)
    {
        stopInFlightNotes();

        targetTracks = std::move (targets);
        events = buildMelody();
        nextEventIndex = 0;
        startTimeMs = juce::Time::getMillisecondCounter();
        startTimerHz (50);
    }

    ~MelodyInjector() override
    {
        stopTimer();
        stopInFlightNotes();
    }

private:
    struct Event { int timeMs; int note; bool isOn; };

    static std::vector<Event> buildMelody()
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

    void stopInFlightNotes()
    {
        for (auto note : heldNotes)
            for (auto* track : targetTracks)
                track->injectTestNote (note, 0.8f, false);

        heldNotes.clear();
    }

    void timerCallback() override
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

    std::vector<TrackBase*> targetTracks;
    std::vector<Event> events;
    std::vector<int> heldNotes;
    int nextEventIndex = 0;
    juce::uint32 startTimeMs = 0;
};

// Substitutes loaded-from-disk audio for live mic input for a while, so
// AudioTrack recording can be exercised without needing to talk into a mic.
class AudioInputSimulator
{
public:
    void start (std::vector<float> samples)
    {
        const juce::ScopedLock lock (dataLock);
        data = std::move (samples);
        playbackIndex = 0;
        active.store (! data.empty());
    }

    bool isActive() const { return active.load(); }

    // Fills destination with the next numSamples of simulated audio
    // (zero-padded once exhausted), deactivating itself when done.
    void fillNextBlock (float* destination, int numSamples)
    {
        const juce::ScopedLock lock (dataLock);

        for (int i = 0; i < numSamples; ++i)
            destination[i] = playbackIndex < data.size() ? data[playbackIndex++] : 0.0f;

        if (playbackIndex >= data.size())
            active.store (false);
    }

private:
    juce::CriticalSection dataLock;
    std::vector<float> data;
    size_t playbackIndex = 0;
    std::atomic<bool> active { false };
};

// Shows and edits the currently selected track's controls (arm, type,
// volume) plus placeholders for features that don't exist yet (bus,
// effects, sound selection) - one shared panel rather than each track
// lane carrying its own full control set.
class TrackInspector : public juce::Component
{
public:
    TrackInspector()
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

    // Pass nullptr to show an empty/disabled state (no track selected).
    void showTrack (TrackBase* track)
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

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::darkslategrey.darker (0.3f));
    }

    void resized() override
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

private:
    TrackBase* currentTrack = nullptr;

    juce::Label headerLabel;
    juce::TextButton armButton;
    juce::TextButton typeButton;
    juce::Slider volumeSlider;
    juce::Label busLabel;
    juce::Label effectsLabel;
    juce::Label soundLabel;
};

class MainComponent : public juce::Component,
                       public juce::AudioIODeviceCallback,
                       private juce::Timer
{
public:
    static constexpr int numTracks = 4;

    MainComponent()
    {
        recordButton.setButtonText ("Record");
        addAndMakeVisible (recordButton);
        recordButton.onClick = [this] { recordButtonClicked(); };

        playButton.setButtonText ("Play");
        addAndMakeVisible (playButton);
        playButton.onClick = [this] { playButtonClicked(); };

        saveButton.setButtonText ("Save");
        addAndMakeVisible (saveButton);
        saveButton.onClick = [this] { saveButtonClicked(); };

        loadButton.setButtonText ("Load");
        addAndMakeVisible (loadButton);
        loadButton.onClick = [this] { loadButtonClicked(); };

        simulateButton.setButtonText ("Simulate Input");
        addAndMakeVisible (simulateButton);
        simulateButton.onClick = [this] { simulateButtonClicked(); };

        simulateAudioButton.setButtonText ("Simulate Audio Input");
        addAndMakeVisible (simulateAudioButton);
        simulateAudioButton.onClick = [this] { simulateAudioButtonClicked(); };

        audioFormatManager.registerBasicFormats();

        timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
        addAndMakeVisible (timeLabel);

        addAndMakeVisible (trackInspector);

        for (int i = 0; i < numTracks; ++i)
        {
            auto track = makeTrack (TrackType::Midi, "Track " + juce::String (i + 1));
            addAndMakeVisible (*track);
            track->onTypeToggleRequested = [this, i] { toggleTrackType (i); };
            track->onSelected = [this, i] { selectTrack (i); };
            tracks.push_back (std::move (track));
        }

        selectTrack (0);

        setSize (1000, 730);

        // JUCE's CoreAudio backend opens the input device directly without
        // ever calling AVFoundation's authorization API, so on some macOS
        // versions the mic permission prompt never appears on its own -
        // request it explicitly so the app shows up under Privacy &
        // Security > Microphone and actually gets asked.
        requestMicrophonePermission ([] (bool /*granted*/) {});

        // Mono mic input, stereo output.
        deviceManager.initialise (1, 2, nullptr, true);
        deviceManager.addAudioCallback (this);
        startTimerHz (30);
    }

    ~MainComponent() override
    {
        stopTimer();
        deviceManager.removeAudioCallback (this);
        deviceManager.closeAudioDevice();
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        currentSampleRate = device->getCurrentSampleRate();

        for (auto& track : tracks)
            track->prepareToPlay (currentSampleRate);

        scratchBuffer.setSize (2, device->getCurrentBufferSizeSamples());
        simulatedInputScratch.setSize (1, device->getCurrentBufferSizeSamples());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                            float* const* outputChannelData, int numOutputChannels,
                                            int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        juce::AudioBuffer<float> outputBuffer (outputChannelData, numOutputChannels, numSamples);
        outputBuffer.clear();

        auto usingSimulatedInput = audioInputSimulator.isActive();

        if (usingSimulatedInput)
            audioInputSimulator.fillNextBlock (simulatedInputScratch.getWritePointer (0), numSamples);

        auto inputBuffer = usingSimulatedInput
                              ? juce::AudioBuffer<const float> (simulatedInputScratch.getArrayOfReadPointers(), 1, numSamples)
                              : juce::AudioBuffer<const float> (inputChannelData, numInputChannels, numSamples);

        auto desiredState = requestedState.load();
        auto transitioned = (desiredState != currentState);
        auto elapsedNow = elapsedSamples.load();

        if (transitioned)
        {
            elapsedNow = 0.0;
            currentState = desiredState;
        }

        {
            // Tracks can be replaced (type toggle, load) from the message
            // thread while this runs, so guard the vector itself; the lock
            // is only ever held briefly and uncontended in the common case.
            const juce::ScopedLock lock (tracksLock);

            for (auto& track : tracks)
                mixTrackIntoOutput (*track, outputBuffer, numSamples, currentState, elapsedNow, &inputBuffer);
        }

        if (currentState != TransportState::Idle)
            elapsedSamples.store (elapsedNow + numSamples);
        else
            elapsedSamples.store (0.0);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::darkslategrey);
    }

    // Drawn after all child track lanes, so the playhead line sits on top
    // of the piano rolls/waveforms instead of being painted over by them.
    void paintOverChildren (juce::Graphics& g) override
    {
        if (tracksArea.isEmpty())
            return;

        auto elapsedSeconds = elapsedSamples.load() / currentSampleRate;
        auto x = tracksArea.getX() + (int) (elapsedSeconds * kPixelsPerSecond);

        if (x < tracksArea.getX() || x > tracksArea.getRight())
            return;

        g.setColour (juce::Colours::red);
        g.drawLine ((float) x, (float) tracksArea.getY(), (float) x, (float) tracksArea.getBottom(), 2.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topRowArea = area.removeFromTop (40);

        juce::FlexBox topRow;
        topRow.flexDirection = juce::FlexBox::Direction::row;
        topRow.items.add (juce::FlexItem (recordButton).withWidth (90).withMargin (5));
        topRow.items.add (juce::FlexItem (playButton).withWidth (90).withMargin (5));
        topRow.items.add (juce::FlexItem (saveButton).withWidth (90).withMargin (5));
        topRow.items.add (juce::FlexItem (loadButton).withWidth (90).withMargin (5));
        topRow.items.add (juce::FlexItem (simulateButton).withWidth (140).withMargin (5));
        topRow.items.add (juce::FlexItem (simulateAudioButton).withWidth (170).withMargin (5));
        topRow.items.add (juce::FlexItem (timeLabel).withWidth (120).withMargin (5));
        topRow.performLayout (topRowArea);

        auto inspectorHeight = juce::jmin (280, area.getHeight() / 3);
        trackInspector.setBounds (area.removeFromBottom (inspectorHeight));

        tracksArea = area;
        auto rowHeight = area.getHeight() / numTracks;

        for (auto& track : tracks)
            track->setBounds (area.removeFromTop (rowHeight));
    }

private:
    void timerCallback() override
    {
        auto elapsedSeconds = elapsedSamples.load() / currentSampleRate;

        double totalSeconds = 0.0;
        for (auto& track : tracks)
            totalSeconds = juce::jmax (totalSeconds, track->getLastEventTimeSamples() / currentSampleRate);

        timeLabel.setText (formatTime (elapsedSeconds) + " / " + formatTime (totalSeconds),
                            juce::dontSendNotification);
        repaint();
    }

    static juce::String formatTime (double seconds)
    {
        auto totalWholeSeconds = (int) seconds;
        auto minutes = totalWholeSeconds / 60;
        auto secs = totalWholeSeconds % 60;
        return juce::String (minutes) + ":" + (secs < 10 ? "0" : "") + juce::String (secs);
    }

    void recordButtonClicked()
    {
        if (requestedState.load() == TransportState::Recording)
        {
            requestedState.store (TransportState::Idle);
            recordButton.setButtonText ("Record");
        }
        else
        {
            requestedState.store (TransportState::Recording);
            recordButton.setButtonText ("Stop Recording");
            playButton.setButtonText ("Play");
        }
    }

    void playButtonClicked()
    {
        if (requestedState.load() == TransportState::Playing)
        {
            requestedState.store (TransportState::Idle);
            playButton.setButtonText ("Play");
        }
        else
        {
            requestedState.store (TransportState::Playing);
            playButton.setButtonText ("Stop");
            recordButton.setButtonText ("Record");
        }
    }

    static juce::File audioFolderFor (const juce::File& projectFile)
    {
        return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + "_Audio");
    }

    void saveButtonClicked()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save Project", juce::File(), "*.captproj");

        auto flags = juce::FileBrowserComponent::saveMode
                   | juce::FileBrowserComponent::canSelectFiles
                   | juce::FileBrowserComponent::warnAboutOverwriting;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            juce::XmlElement root ("CAPT_PROJECT");
            auto audioFolder = audioFolderFor (file);

            for (auto& track : tracks)
                root.addChildElement (track->toXml (audioFolder).release());

            root.writeTo (file);
        });
    }

    void loadButtonClicked()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load Project", juce::File(), "*.captproj");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            auto xml = juce::XmlDocument::parse (file);
            if (xml == nullptr)
                return;

            auto audioFolder = audioFolderFor (file);
            int index = 0;

            for (auto* trackXml : xml->getChildIterator())
            {
                if (index >= (int) tracks.size())
                    break;

                auto desiredType = trackTypeForXmlTag (trackXml->getTagName());

                if (tracks[(size_t) index]->getType() != desiredType)
                    replaceTrack (index, desiredType);

                tracks[(size_t) index]->fromXml (*trackXml, audioFolder);
                ++index;
            }
        });
    }

    void replaceTrack (int index, TrackType newType)
    {
        auto newTrack = makeTrack (newType, "Track " + juce::String (index + 1));
        newTrack->prepareToPlay (currentSampleRate);
        newTrack->onTypeToggleRequested = [this, index] { toggleTrackType (index); };
        newTrack->onSelected = [this, index] { selectTrack (index); };
        addAndMakeVisible (*newTrack);

        std::unique_ptr<TrackBase> oldTrack;

        {
            const juce::ScopedLock lock (tracksLock);
            oldTrack = std::move (tracks[(size_t) index]);
            tracks[(size_t) index] = std::move (newTrack);
        }

        removeChildComponent (oldTrack.get());
        resized();

        // The replaced track is a new object - re-point the inspector and
        // selection highlight at it (this is a no-op for any other index).
        selectTrack (selectedTrackIndex);
    }

    void toggleTrackType (int index)
    {
        auto newType = tracks[(size_t) index]->getType() == TrackType::Midi ? TrackType::Audio : TrackType::Midi;
        replaceTrack (index, newType);
    }

    void selectTrack (int index)
    {
        selectedTrackIndex = index;

        for (size_t i = 0; i < tracks.size(); ++i)
            tracks[i]->setSelected ((int) i == index);

        trackInspector.showTrack (tracks[(size_t) index].get());
    }

    void simulateButtonClicked()
    {
        std::vector<TrackBase*> targets;

        for (auto& track : tracks)
            if (track->isArmed())
                targets.push_back (track.get());

        if (targets.empty())
            for (auto& track : tracks)
                targets.push_back (track.get());

        melodyInjector.start (targets);
    }

    void simulateAudioButtonClicked()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Choose an audio file to simulate as input",
                                                             juce::File(), "*.wav;*.mp3;*.aiff;*.aif");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
        {
            auto file = chooser.getResult();
            if (file == juce::File())
                return;

            std::unique_ptr<juce::AudioFormatReader> reader (audioFormatManager.createReaderFor (file));
            if (reader == nullptr)
                return;

            juce::AudioBuffer<float> fileBuffer ((int) reader->numChannels, (int) reader->lengthInSamples);
            reader->read (&fileBuffer, 0, (int) reader->lengthInSamples, 0, true, true);

            // Downmix to mono - the rest of the audio pipeline (recording,
            // playback) only ever deals in a single channel.
            std::vector<float> monoSamples ((size_t) fileBuffer.getNumSamples());

            for (int i = 0; i < fileBuffer.getNumSamples(); ++i)
            {
                float sum = 0.0f;

                for (int channel = 0; channel < fileBuffer.getNumChannels(); ++channel)
                    sum += fileBuffer.getSample (channel, i);

                monoSamples[(size_t) i] = sum / (float) fileBuffer.getNumChannels();
            }

            audioInputSimulator.start (std::move (monoSamples));
        });
    }

    void mixTrackIntoOutput (TrackBase& track, juce::AudioBuffer<float>& outputBuffer, int numSamples,
                              TransportState globalState, double transportElapsedSamples,
                              const juce::AudioBuffer<const float>* inputBuffer)
    {
        scratchBuffer.clear (0, numSamples);
        track.renderNextBlock (scratchBuffer, 0, numSamples, globalState, transportElapsedSamples, inputBuffer);

        auto gain = track.getVolume();

        for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            outputBuffer.addFrom (channel, 0, scratchBuffer, channel, 0, numSamples, gain);
    }

    juce::TextButton recordButton, playButton, saveButton, loadButton, simulateButton, simulateAudioButton;
    juce::Label timeLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    MelodyInjector melodyInjector;
    juce::AudioFormatManager audioFormatManager;
    AudioInputSimulator audioInputSimulator;
    juce::AudioBuffer<float> simulatedInputScratch;

    std::atomic<TransportState> requestedState { TransportState::Idle };
    TransportState currentState = TransportState::Idle;
    std::atomic<double> elapsedSamples { 0.0 };
    double currentSampleRate = 44100.0;

    std::vector<std::unique_ptr<TrackBase>> tracks;
    juce::Rectangle<int> tracksArea;
    TrackInspector trackInspector;
    int selectedTrackIndex = 0;
    juce::CriticalSection tracksLock;
    juce::AudioBuffer<float> scratchBuffer;
    juce::AudioDeviceManager deviceManager;
};

class CAPTApplication : public juce::JUCEApplication
{
public:
    CAPTApplication() = default;

    const juce::String getApplicationName() override    { return "CAPT"; }
    const juce::String getApplicationVersion() override  { return "0.1.0"; }

    void initialise (const juce::String&) override
    {
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name,
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour (juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (CAPTApplication)
