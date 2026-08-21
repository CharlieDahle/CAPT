#include "MainComponent.h"
#include "../Tracks/TrackFactory.h"
#include "../Project/ProjectFile.h"
#include <iterator>

MainComponent::MainComponent()
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

    metronomeButton.setButtonText ("Metronome");
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setToggleState (false, juce::dontSendNotification);
    metronomeButton.onClick = [this] { engine.setMetronomeEnabled (metronomeButton.getToggleState()); };
    addAndMakeVisible (metronomeButton);

    audioFormatManager.registerBasicFormats();

    timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
    addAndMakeVisible (timeLabel);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    addAndMakeVisible (bpmLabel);

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setRange (20.0, 300.0, 1.0);
    bpmSlider.setValue (engine.getTempo().bpm, juce::dontSendNotification);
    bpmSlider.onValueChange = [this]
    {
        auto tempo = engine.getTempo();
        tempo.bpm = bpmSlider.getValue();
        engine.setTempo (tempo);
        repaint();
    };
    addAndMakeVisible (bpmSlider);

    timeSignatureBox.addItem ("4/4", 1);
    timeSignatureBox.addItem ("3/4", 2);
    timeSignatureBox.addItem ("2/4", 3);
    timeSignatureBox.addItem ("6/8", 4);
    timeSignatureBox.addItem ("5/4", 5);
    timeSignatureBox.addItem ("7/8", 6);
    timeSignatureBox.setSelectedId (1, juce::dontSendNotification);
    timeSignatureBox.onChange = [this]
    {
        static const TimeSignature options[] = { { 4, 4 }, { 3, 4 }, { 2, 4 }, { 6, 8 }, { 5, 4 }, { 7, 8 } };
        auto index = timeSignatureBox.getSelectedId() - 1;

        if (index >= 0 && index < (int) std::size (options))
        {
            auto tempo = engine.getTempo();
            tempo.timeSignature = options[(size_t) index];
            engine.setTempo (tempo);
        }

        repaint();
    };
    addAndMakeVisible (timeSignatureBox);

    gridResolutionBox.addItem ("1/4", 1);
    gridResolutionBox.addItem ("1/8", 2);
    gridResolutionBox.addItem ("1/16", 3);
    gridResolutionBox.addItem ("1/32", 4);
    gridResolutionBox.setSelectedId (3, juce::dontSendNotification);
    gridResolutionBox.onChange = [this]
    {
        static const int options[] = { 1, 2, 4, 8 };
        auto index = gridResolutionBox.getSelectedId() - 1;

        if (index >= 0 && index < (int) std::size (options))
            gridStepsPerBeat = options[(size_t) index];

        repaint();
    };
    addAndMakeVisible (gridResolutionBox);

    metronomeVolumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    metronomeVolumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
    metronomeVolumeSlider.setRange (0.0, 1.0);
    metronomeVolumeSlider.setValue (0.3, juce::dontSendNotification);
    metronomeVolumeSlider.onValueChange = [this] { engine.setMetronomeGain ((float) metronomeVolumeSlider.getValue()); };
    addAndMakeVisible (metronomeVolumeSlider);

    addAndMakeVisible (timelineRuler);
    timelineRuler.setGridSettingsProvider ([this] { return GridSettings { engine.getTempo().timeSignature, gridStepsPerBeat }; });

    addAndMakeVisible (trackInspector);
    trackInspector.setGridSettingsProvider ([this] { return GridSettings { engine.getTempo().timeSignature, gridStepsPerBeat }; });

    tracksContainer.attachTracks (&engine.getTracks());
    tracksViewport.setViewedComponent (&tracksContainer, false);
    tracksViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (tracksViewport);

    for (int i = 0; i < numTracks; ++i)
    {
        auto track = makeTrack (TrackType::Midi, "Track " + juce::String (i + 1));
        tracksContainer.addAndMakeVisible (*track);
        track->onTypeToggleRequested = [this, i] { toggleTrackType (i); };
        track->onSelected = [this, i] { selectTrack (i); };
        track->onExpandToggleRequested = [this, i] { toggleExpand (i); };
        track->setGridSettingsProvider ([this] { return GridSettings { engine.getTempo().timeSignature, gridStepsPerBeat }; });
        track->setTempoProvider ([this] { return engine.getTempo(); });
        engine.addTrack (std::move (track));
    }

    selectTrack (0);

    setSize (1000, 730);

    engine.start();

    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::darkslategrey);
}

void MainComponent::paintOverChildren (juce::Graphics& g)
{
    if (tracksArea.isEmpty())
        return;

    // Grid lines are drawn per-track (MidiTrack via its PianoRollCanvas,
    // AudioTrack in its own paint()) rather than as one overlay here, so
    // they naturally stay confined to each track's content area - never
    // crossing a header, never extending past the last track.
    auto elapsedSeconds = engine.getElapsedSamples() / engine.getCurrentSampleRate();
    auto elapsedBeats = secondsToBeats (elapsedSeconds, engine.getTempo());
    auto x = tracksArea.getX() + (int) kKeyboardStripWidth + (int) (elapsedBeats * kPixelsPerBeat);

    if (x < tracksArea.getX() || x > tracksArea.getRight())
        return;

    g.setColour (juce::Colours::red);
    g.drawLine ((float) x, (float) tracksArea.getY(), (float) x, (float) tracksArea.getBottom(), 2.0f);
}

void MainComponent::resized()
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
    topRow.items.add (juce::FlexItem (metronomeButton).withWidth (110).withMargin (5));
    topRow.items.add (juce::FlexItem (timeLabel).withWidth (120).withMargin (5));
    topRow.performLayout (topRowArea);

    auto gridRowArea = area.removeFromTop (32);
    juce::FlexBox gridRow;
    gridRow.flexDirection = juce::FlexBox::Direction::row;
    gridRow.items.add (juce::FlexItem (bpmLabel).withWidth (36).withMargin (4));
    gridRow.items.add (juce::FlexItem (bpmSlider).withWidth (110).withMargin (4));
    gridRow.items.add (juce::FlexItem (timeSignatureBox).withWidth (80).withMargin (4));
    gridRow.items.add (juce::FlexItem (gridResolutionBox).withWidth (80).withMargin (4));
    gridRow.items.add (juce::FlexItem (metronomeVolumeSlider).withWidth (140).withMargin (4));
    gridRow.performLayout (gridRowArea);

    auto inspectorHeight = juce::jmin (280, area.getHeight() / 3);
    trackInspector.setBounds (area.removeFromBottom (inspectorHeight));

    timelineRuler.setBounds (area.removeFromTop (24));

    tracksArea = area;
    tracksViewport.setBounds (area);
    tracksContainer.setSize (area.getWidth(), tracksContainer.computeTotalHeight());
}

void MainComponent::timerCallback()
{
    auto elapsedSeconds = engine.getElapsedSamples() / engine.getCurrentSampleRate();

    double totalSeconds = 0.0;
    for (auto& track : engine.getTracks())
        totalSeconds = juce::jmax (totalSeconds, track->getLastEventTimeSamples() / engine.getCurrentSampleRate());

    timeLabel.setText (formatTime (elapsedSeconds) + " / " + formatTime (totalSeconds),
                        juce::dontSendNotification);

    repaintPlayhead (elapsedSeconds);
}

void MainComponent::repaintPlayhead (double elapsedSeconds)
{
    if (tracksArea.isEmpty())
        return;

    auto elapsedBeats = secondsToBeats (elapsedSeconds, engine.getTempo());
    auto x = tracksArea.getX() + (int) kKeyboardStripWidth + (int) (elapsedBeats * kPixelsPerBeat);
    auto previous = previousPlayheadX < 0 ? x : previousPlayheadX;
    auto lo = juce::jmin (x, previous) - 3;
    auto hi = juce::jmax (x, previous) + 3;

    repaint (juce::Rectangle<int> (lo, tracksArea.getY(), hi - lo, tracksArea.getHeight()));

    previousPlayheadX = x;
}

juce::String MainComponent::formatTime (double seconds)
{
    auto totalWholeSeconds = (int) seconds;
    auto minutes = totalWholeSeconds / 60;
    auto secs = totalWholeSeconds % 60;
    return juce::String (minutes) + ":" + (secs < 10 ? "0" : "") + juce::String (secs);
}

void MainComponent::recordButtonClicked()
{
    if (engine.getRequestedState() == TransportState::Recording)
    {
        engine.requestState (TransportState::Idle);
        recordButton.setButtonText ("Record");
    }
    else
    {
        engine.requestState (TransportState::Recording);
        recordButton.setButtonText ("Stop Recording");
        playButton.setButtonText ("Play");
    }
}

void MainComponent::playButtonClicked()
{
    if (engine.getRequestedState() == TransportState::Playing)
    {
        engine.requestState (TransportState::Idle);
        playButton.setButtonText ("Play");
    }
    else
    {
        engine.requestState (TransportState::Playing);
        playButton.setButtonText ("Stop");
        recordButton.setButtonText ("Record");
    }
}

void MainComponent::saveButtonClicked()
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

        ProjectFile::save (file, engine.getTracks());
    });
}

void MainComponent::loadButtonClicked()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load Project", juce::File(), "*.captproj");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (flags, [this] (const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        if (file == juce::File())
            return;

        ProjectFile::load (file, engine.getTracks(),
                            [this] (int index, TrackType desiredType) { replaceTrack (index, desiredType); });
    });
}

void MainComponent::replaceTrack (int index, TrackType newType)
{
    auto newTrack = makeTrack (newType, "Track " + juce::String (index + 1));
    newTrack->onTypeToggleRequested = [this, index] { toggleTrackType (index); };
    newTrack->onSelected = [this, index] { selectTrack (index); };
    newTrack->onExpandToggleRequested = [this, index] { toggleExpand (index); };
    newTrack->setGridSettingsProvider ([this] { return GridSettings { engine.getTempo().timeSignature, gridStepsPerBeat }; });
    newTrack->setTempoProvider ([this] { return engine.getTempo(); });
    newTrack->setExpanded (index == expandedTrackIndex);
    tracksContainer.addAndMakeVisible (*newTrack);

    auto oldTrack = engine.replaceTrack (index, std::move (newTrack));

    tracksContainer.removeChildComponent (oldTrack.get());

    // resized() below calls tracksContainer.setSize(), but a type swap
    // never changes track count or which index is expanded, so the total
    // height is always identical before/after - setSize() is a no-op in
    // that case and never cascades into TracksContainer::resized() (same
    // gotcha documented on TracksContainer::setExpandedIndex), leaving the
    // freshly-added replacement track at its default zero size. Lay it out
    // directly rather than relying on that cascade.
    tracksContainer.resized();
    resized();

    // The replaced track is a new object - re-point the inspector and
    // selection highlight at it (this is a no-op for any other index).
    selectTrack (selectedTrackIndex);
}

void MainComponent::toggleTrackType (int index)
{
    auto newType = engine.getTracks()[(size_t) index]->getType() == TrackType::Midi ? TrackType::Audio : TrackType::Midi;
    replaceTrack (index, newType);
}

void MainComponent::selectTrack (int index)
{
    selectedTrackIndex = index;

    auto& tracks = engine.getTracks();
    for (size_t i = 0; i < tracks.size(); ++i)
        tracks[i]->setSelected ((int) i == index);

    trackInspector.showTrack (tracks[(size_t) index].get());
}

void MainComponent::toggleExpand (int index)
{
    expandedTrackIndex = (expandedTrackIndex == index) ? -1 : index;

    auto& tracks = engine.getTracks();
    for (size_t i = 0; i < tracks.size(); ++i)
        tracks[i]->setExpanded ((int) i == expandedTrackIndex);

    tracksContainer.setExpandedIndex (expandedTrackIndex);
    resized();
}

void MainComponent::simulateButtonClicked()
{
    std::vector<TrackBase*> targets;

    for (auto& track : engine.getTracks())
        if (track->isArmed())
            targets.push_back (track.get());

    if (targets.empty())
        for (auto& track : engine.getTracks())
            targets.push_back (track.get());

    melodyInjector.start (targets);
}

void MainComponent::simulateAudioButtonClicked()
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

        engine.startSimulatingAudioInput (std::move (monoSamples));
    });
}
