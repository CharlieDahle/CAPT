#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "../Core/Tempo.h"
#include "../Audio/AudioEngine.h"
#include "../UI/TracksContainer.h"
#include "../UI/TrackInspector.h"
#include "../UI/TimelineRuler.h"
#include "../UI/MidiKeyboardWindow.h"
#include "../Dev/MelodyInjector.h"

class MainComponent : public juce::Component,
                       private juce::Timer,
                       private juce::MidiKeyboardState::Listener
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

    // Cmd/Ctrl+K: closed -> open (and focus); open but this window has
    // focus (i.e. the popup doesn't) -> bring the popup's keyboard into
    // focus; open and already focused is handled on the popup's own side
    // (MidiKeyboardWindow::Content::keyPressed), since that's the only
    // window a keypress meaning "close" could arrive on.
    bool keyPressed (const juce::KeyPress& key) override;

private:
    void timerCallback() override;

    // Forwards notes played on the popup MIDI keyboard to whichever tracks
    // are armed (or all of them, if none are) - same target selection as
    // simulateButtonClicked, just live instead of a canned melody.
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    std::vector<TrackBase*> armedOrAllTrackTargets();
    std::vector<TrackBase*> liveKeyboardTargets();

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
    void midiKeyboardButtonClicked();

    juce::TextButton recordButton, playButton, saveButton, loadButton, simulateButton, simulateAudioButton;
    juce::TextButton midiKeyboardButton;
    juce::MidiKeyboardState popupKeyboardState;
    std::unique_ptr<MidiKeyboardWindow> midiKeyboardWindow;
    juce::Label timeLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;
    MelodyInjector melodyInjector;
    juce::AudioFormatManager audioFormatManager;

    // Owned here (tracks are juce::Components, so they belong in the
    // component tree's owner), not by AudioEngine - AudioEngine only holds
    // non-owning pointers to them, registered via addTrack/replaceTrackAt.
    // trackInspector is declared alongside them for the same reason: it owns
    // the WaveformDisplay that engine.setVisualiserTarget() points the audio
    // thread at (via the AudioTap interface), so it's just as much "a thing
    // the audio thread can reach into" as the tracks themselves.
    //
    // Declaration order matters: members are destroyed in *reverse* of
    // declaration order, and AudioEngine's destructor blocks until the audio
    // thread will never call into it again. Declaring tracks and
    // trackInspector before engine means engine is destroyed first
    // (guaranteeing the audio thread has stopped touching whatever it was
    // pointing at) and only then are the actual objects it was pointing at
    // freed - never before.
    std::vector<std::unique_ptr<TrackBase>> tracks;
    TrackInspector trackInspector;
    AudioEngine engine;

    juce::TextButton metronomeButton;
    juce::Label bpmLabel;
    juce::Slider bpmSlider;
    juce::ComboBox timeSignatureBox;
    juce::ComboBox gridResolutionBox;
    juce::Slider metronomeVolumeSlider;
    TimelineRuler timelineRuler;
    int gridStepsPerBeat = 4;

    juce::Rectangle<int> tracksArea;
    int previousPlayheadX = -1;
    juce::Viewport tracksViewport;
    TracksContainer tracksContainer;
    int selectedTrackIndex = 0;
    int expandedTrackIndex = -1;
};
