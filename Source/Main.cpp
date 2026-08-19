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

        recordButton.setButtonText ("Record");
        addAndMakeVisible (recordButton);
        recordButton.onClick = [this] { recordButtonClicked(); };

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

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        auto desiredState = requestedState.load();
        if (desiredState != currentState)
        {
            if (desiredState == TransportState::Recording)
                numRecordedEvents = 0;
            else if (desiredState == TransportState::Playing)
                nextPlaybackIndex = 0;

            elapsedSamples = 0.0;
            currentState = desiredState;
        }

        juce::MidiBuffer liveMidi;
        keyboardState.processNextMidiBuffer (liveMidi, startSample, numSamples, true);

        juce::MidiBuffer midiForSynth;

        if (currentState == TransportState::Playing)
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

            if (nextPlaybackIndex >= numRecordedEvents)
            {
                currentState = TransportState::Idle;
                requestedState.store (TransportState::Idle);
            }
        }
        else
        {
            midiForSynth = liveMidi;
        }

        if (currentState == TransportState::Recording)
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

        elapsedSamples += numSamples;
    }

    float getVolume() const { return volume.load(); }

    void setPlaying (bool shouldPlay)
    {
        requestedState.store (shouldPlay ? TransportState::Playing : TransportState::Idle);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        keyboardComponent.setBounds (area.removeFromBottom (70));

        auto topRow = area.removeFromTop (30);
        nameLabel.setBounds (topRow.removeFromLeft (80));
        recordButton.setBounds (topRow.removeFromLeft (100).reduced (3));
        volumeSlider.setBounds (topRow.reduced (3));
    }

private:
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
        }
    }

    juce::Label nameLabel;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboardComponent;
    juce::Synthesiser synth;
    juce::TextButton recordButton;
    juce::Slider volumeSlider;

    std::atomic<float> volume { 0.8f };
    std::atomic<TransportState> requestedState { TransportState::Idle };

    TransportState currentState = TransportState::Idle;
    double elapsedSamples = 0.0;
    int nextPlaybackIndex = 0;

    static constexpr int maxRecordedEvents = 4096;
    std::array<RecordedNoteEvent, maxRecordedEvents> recordedEvents;
    int numRecordedEvents = 0;
};

class MainComponent : public juce::AudioAppComponent
{
public:
    static constexpr int numTracks = 4;

    MainComponent()
    {
        globalPlayButton.setButtonText ("Play");
        addAndMakeVisible (globalPlayButton);
        globalPlayButton.onClick = [this] { globalPlayButtonClicked(); };

        for (int i = 0; i < numTracks; ++i)
        {
            auto track = std::make_unique<Track> ("Track " + juce::String (i + 1));
            addAndMakeVisible (*track);
            tracks.push_back (std::move (track));
        }

        setSize (700, 740);

        setAudioChannels (0, 2);
    }

    ~MainComponent() override
    {
        shutdownAudio();
    }

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override
    {
        for (auto& track : tracks)
            track->prepareToPlay (sampleRate);

        scratchBuffer.setSize (2, samplesPerBlockExpected);
    }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        bufferToFill.clearActiveBufferRegion();

        for (auto& track : tracks)
            mixTrackIntoOutput (*track, bufferToFill);
    }

    void releaseResources() override {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::darkslategrey);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        globalPlayButton.setBounds (area.removeFromTop (40).reduced (5));

        auto rowHeight = area.getHeight() / numTracks;

        for (auto& track : tracks)
            track->setBounds (area.removeFromTop (rowHeight));
    }

private:
    void globalPlayButtonClicked()
    {
        isPlaying = ! isPlaying;

        for (auto& track : tracks)
            track->setPlaying (isPlaying);

        globalPlayButton.setButtonText (isPlaying ? "Stop" : "Play");
    }

    void mixTrackIntoOutput (Track& track, const juce::AudioSourceChannelInfo& bufferToFill)
    {
        scratchBuffer.clear (0, bufferToFill.numSamples);
        track.renderNextBlock (scratchBuffer, 0, bufferToFill.numSamples);

        auto gain = track.getVolume();

        for (int channel = 0; channel < bufferToFill.buffer->getNumChannels(); ++channel)
        {
            bufferToFill.buffer->addFrom (channel, bufferToFill.startSample,
                                           scratchBuffer, channel, 0,
                                           bufferToFill.numSamples, gain);
        }
    }

    juce::TextButton globalPlayButton;
    bool isPlaying = false;

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
