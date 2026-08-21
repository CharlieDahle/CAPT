#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "../Audio/AudioEngine.h"
#include "../UI/TracksContainer.h"
#include "../UI/TrackInspector.h"
#include "../Dev/MelodyInjector.h"

class MainComponent : public juce::Component,
                       private juce::Timer
{
public:
    static constexpr int numTracks = 4;

    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;

    // Drawn after all child track lanes, so the playhead line sits on top
    // of the piano rolls/waveforms instead of being painted over by them.
    void paintOverChildren (juce::Graphics& g) override;

    void resized() override;

private:
    void timerCallback() override;

    // Calling repaint() with no args at 30Hz invalidated this component's
    // *entire* area, forcing every track's piano roll to fully redraw 30
    // times a second just to move a 2px line - real CPU cost that can
    // starve the real-time audio thread. Only invalidate the thin strip
    // the line actually moved through instead.
    void repaintPlayhead (double elapsedSeconds);

    static juce::String formatTime (double seconds);

    void recordButtonClicked();
    void playButtonClicked();
    void saveButtonClicked();
    void loadButtonClicked();
    void replaceTrack (int index, TrackType newType);
    void toggleTrackType (int index);
    void selectTrack (int index);
    void toggleExpand (int index);
    void simulateButtonClicked();
    void simulateAudioButtonClicked();

    juce::TextButton recordButton, playButton, saveButton, loadButton, simulateButton, simulateAudioButton;
    juce::Label timeLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    MelodyInjector melodyInjector;
    juce::AudioFormatManager audioFormatManager;

    AudioEngine engine;

    juce::Rectangle<int> tracksArea;
    int previousPlayheadX = -1;
    juce::Viewport tracksViewport;
    TracksContainer tracksContainer;
    TrackInspector trackInspector;
    int selectedTrackIndex = 0;
    int expandedTrackIndex = -1;
};
