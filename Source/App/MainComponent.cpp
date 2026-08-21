#include "MainComponent.h"
#include "../Tracks/TrackFactory.h"
#include "../Project/ProjectFile.h"

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

    audioFormatManager.registerBasicFormats();

    timeLabel.setText ("0:00 / 0:00", juce::dontSendNotification);
    addAndMakeVisible (timeLabel);

    addAndMakeVisible (trackInspector);

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

    auto elapsedSeconds = engine.getElapsedSamples() / engine.getCurrentSampleRate();
    auto x = tracksArea.getX() + (int) kKeyboardStripWidth + (int) (elapsedSeconds * kPixelsPerSecond);

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
    topRow.items.add (juce::FlexItem (timeLabel).withWidth (120).withMargin (5));
    topRow.performLayout (topRowArea);

    auto inspectorHeight = juce::jmin (280, area.getHeight() / 3);
    trackInspector.setBounds (area.removeFromBottom (inspectorHeight));

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

    auto x = tracksArea.getX() + (int) kKeyboardStripWidth + (int) (elapsedSeconds * kPixelsPerSecond);
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
    newTrack->setExpanded (index == expandedTrackIndex);
    tracksContainer.addAndMakeVisible (*newTrack);

    auto oldTrack = engine.replaceTrack (index, std::move (newTrack));

    tracksContainer.removeChildComponent (oldTrack.get());
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
