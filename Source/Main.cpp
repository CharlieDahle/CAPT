#include <juce_audio_utils/juce_audio_utils.h>

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

struct RecordedNoteEvent
{
    double timeStampSamples = 0.0;
    int noteNumber = 0;
    float velocity = 0.0f;
    bool isNoteOn = false;
};

struct Track : public juce::Component
{
    explicit Track (const juce::String& trackName)
        : keyboardComponent (keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
    {
        nameLabel.setText (trackName, juce::dontSendNotification);
        addAndMakeVisible (nameLabel);

        armButton.setButtonText ("Arm");
        addAndMakeVisible (armButton);
        armButton.onClick = [this]
        {
            auto newArmed = ! armed.load();
            armed.store (newArmed);
            armButton.setButtonText (newArmed ? "Armed" : "Arm");
        };

        volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
        volumeSlider.setRange (0.0, 1.0);
        volumeSlider.setValue (0.8);
        volumeSlider.onValueChange = [this]
        {
            volume.store ((float) volumeSlider.getValue());
        };
        addAndMakeVisible (volumeSlider);

        addAndMakeVisible (keyboardComponent);

        for (int i = 0; i < 8; ++i)
            synth.addVoice (new SineWaveVoice());
        synth.addSound (new SineWaveSound());
    }

    void prepareToPlay (double sampleRate)
    {
        synth.setCurrentPlaybackSampleRate (sampleRate);
    }

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           TransportState globalState, double elapsedSamples)
    {
        auto isArmed = armed.load();
        auto effectiveMode = globalState;

        if (globalState == TransportState::Recording && ! isArmed)
            effectiveMode = TransportState::Playing;

        if (effectiveMode != previousEffectiveMode)
        {
            // Any note that was mid-hold when the mode changed (transport
            // stopped, or arm toggled mid-recording) never gets its
            // matching note-off, since that came from playback/recording
            // logic that's about to stop running - so tell the synth
            // directly, or the voice would hold forever.
            synth.allNotesOff (1, false);

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

                ++numRecordedEvents;
            }
        }

        synth.renderNextBlock (buffer, midiForSynth, startSample, numSamples);
    }

    float getVolume() const { return volume.load(); }

    double getLastEventTimeSamples() const
    {
        auto count = numRecordedEvents.load();
        return count > 0 ? recordedEvents[(size_t) (count - 1)].timeStampSamples : 0.0;
    }

    std::unique_ptr<juce::XmlElement> toXml() const
    {
        auto trackXml = std::make_unique<juce::XmlElement> ("TRACK");
        trackXml->setAttribute ("volume", (double) volume.load());

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

    void fromXml (const juce::XmlElement& trackXml)
    {
        auto newVolume = (float) trackXml.getDoubleAttribute ("volume", 0.8);
        volume.store (newVolume);
        volumeSlider.setValue (newVolume, juce::dontSendNotification);

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

    void resized() override
    {
        auto area = getLocalBounds();
        keyboardComponent.setBounds (area.removeFromBottom (70));

        auto topRow = area.removeFromTop (30);
        nameLabel.setBounds (topRow.removeFromLeft (80));
        armButton.setBounds (topRow.removeFromLeft (100).reduced (3));
        volumeSlider.setBounds (topRow.reduced (3));
    }

private:
    juce::Label nameLabel;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::Synthesiser synth;
    juce::TextButton armButton;
    juce::Slider volumeSlider;

    std::atomic<float> volume { 0.8f };
    std::atomic<bool> armed { false };

    int nextPlaybackIndex = 0;
    TransportState previousEffectiveMode = TransportState::Idle;

    static constexpr int maxRecordedEvents = 4096;
    std::array<RecordedNoteEvent, maxRecordedEvents> recordedEvents;
    std::atomic<int> numRecordedEvents { 0 };
};

class MainComponent : public juce::AudioAppComponent,
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

        timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
        addAndMakeVisible (timeLabel);

        for (int i = 0; i < numTracks; ++i)
        {
            auto track = std::make_unique<Track> ("Track " + juce::String (i + 1));
            addAndMakeVisible (*track);
            tracks.push_back (std::move (track));
        }

        setSize (700, 740);

        setAudioChannels (0, 2);
        startTimerHz (4);
    }

    ~MainComponent() override
    {
        stopTimer();
        shutdownAudio();
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        currentSampleRate = sampleRate;

        for (auto& track : tracks)
            track->prepareToPlay (sampleRate);

        scratchBuffer.setSize (2, samplesPerBlockExpected);
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        auto desiredState = requestedState.load();
        auto transitioned = (desiredState != currentState);
        auto elapsedNow = elapsedSamples.load();

        if (transitioned)
        {
            elapsedNow = 0.0;
            currentState = desiredState;
        }

        for (auto& track : tracks)
            mixTrackIntoOutput (*track, bufferToFill, currentState, elapsedNow);

        if (currentState != TransportState::Idle)
            elapsedSamples.store (elapsedNow + bufferToFill.numSamples);
        else
            elapsedSamples.store (0.0);
    }

    void releaseResources() override {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::darkslategrey);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto topRow = area.removeFromTop (40);
        recordButton.setBounds (topRow.removeFromLeft (90).reduced (5));
        playButton.setBounds (topRow.removeFromLeft (90).reduced (5));
        saveButton.setBounds (topRow.removeFromLeft (90).reduced (5));
        loadButton.setBounds (topRow.removeFromLeft (90).reduced (5));
        timeLabel.setBounds (topRow.removeFromLeft (120).reduced (5));

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

            for (auto& track : tracks)
                root.addChildElement (track->toXml().release());

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

            int index = 0;

            for (auto* trackXml : xml->getChildIterator())
            {
                if (index >= (int) tracks.size())
                    break;

                tracks[(size_t) index]->fromXml (*trackXml);
                ++index;
            }
        });
    }

    void mixTrackIntoOutput (Track& track, const juce::AudioSourceChannelInfo& bufferToFill,
                              TransportState globalState, double transportElapsedSamples)
    {
        scratchBuffer.clear (0, bufferToFill.numSamples);
        track.renderNextBlock (scratchBuffer, 0, bufferToFill.numSamples,
                                globalState, transportElapsedSamples);

        auto gain = track.getVolume();

        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
        {
            bufferToFill.buffer->addFrom (channel, bufferToFill.startSample,
                                           scratchBuffer, channel, 0,
                                           bufferToFill.numSamples, gain);
        }
    }

    juce::TextButton recordButton, playButton, saveButton, loadButton;
    juce::Label timeLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::atomic<TransportState> requestedState { TransportState::Idle };
    TransportState currentState = TransportState::Idle;
    std::atomic<double> elapsedSamples { 0.0 };
    double currentSampleRate = 44100.0;

    std::vector<std::unique_ptr<Track>> tracks;
    juce::AudioBuffer<float> scratchBuffer;
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
