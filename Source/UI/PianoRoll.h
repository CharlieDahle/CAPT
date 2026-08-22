#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "../Core/Tempo.h"

// Draws a track's recorded MIDI notes as horizontal bars, editable with the
// mouse only while editableProvider says the transport is idle - that's
// what makes this safe without locking, since the audio thread only touches
// note storage while recording/playing.
//
// Two modes, switched by expandedFlag: collapsed is a non-interactive
// thumbnail; expanded uses a bigger fixed row height inside a Viewport
// (owned by PianoRollView) so it can scroll through the pitch range.
class PianoRollCanvas : public juce::Component, private juce::Timer
{
public:
    struct Note
    {
        int pitch = 60;
        double startBeats = 0.0;
        double endBeats = 1.0;
        float velocity = 0.8f;

        // Transient UI-only state - never read by MidiTrack::setNotesFromEditor,
        // so it never round-trips into recorded events or the project file.
        // Reset to false for free every time `notes` is rebuilt from
        // notesProvider(), since that always constructs fresh Note values.
        bool selected = false;
    };

    PianoRollCanvas();

    void setNotesProvider (std::function<std::vector<Note>()> provider) { notesProvider = std::move (provider); }
    void setEditCallback (std::function<void (std::vector<Note>)> callback) { editCallback = std::move (callback); }
    void setEditableProvider (std::function<bool()> provider) { editableProvider = std::move (provider); }

    void setLaneClickHandler (std::function<void()> handler) { laneClickHandler = std::move (handler); }
    void setGridSettingsProvider (std::function<GridSettings()> provider) { gridSettingsProvider = std::move (provider); }

    // Where Cmd+V drops pasted notes.
    void setPlayheadBeatsProvider (std::function<double()> provider) { playheadBeatsProvider = std::move (provider); }

    // Forces an immediate pull from notesProvider(), bypassing the timer's
    // steady-state-idle skip - needed right after something outside this
    // component's own edits (project load) writes new data into the track.
    void refreshNotes();

    void setSelectedFlag (bool newSelected) { selectedFlag = newSelected; }

    void setExpandedFlag (bool newExpanded)
    {
        expandedFlag = newExpanded;
        repaint();
    }

    int totalHeightNeeded() const
    {
        return (int) (fixedExpandedRowHeight * (float) (highestNote - lowestNote + 1));
    }

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Repaints so the ghost-note placement preview (see paint()) tracks the
    // cursor. Modifier-key-only changes with a stationary mouse are instead
    // caught by timerCallback's unconditional repaint.
    void mouseMove (const juce::MouseEvent&) override { repaint(); }

    // Delete/Backspace, Cmd+C/X/V/A, Escape - all gated on the same
    // isEditable()/selected/expanded check everything else here uses.
    // Returns false (unhandled) for anything not listed above, so it bubbles
    // up to MainComponent - that's what keeps Cmd+K working regardless of
    // which piano roll currently has keyboard focus.
    bool keyPressed (const juce::KeyPress& key) override;

    // JUCE calls this whenever it re-checks what the cursor should look
    // like (mouse movement, or the periodic updateMouseCursor() poll in
    // timerCallback - modifier-key-only changes don't fire a mouse event on
    // their own, so this component's timer forces a re-check instead of
    // waiting for the mouse to move). No position parameter is available,
    // so this can't tell the keyboard-strip gutter apart from the rest -
    // acceptable, it's a small area and the cursor's just a hint either way.
    juce::MouseCursor getMouseCursor() override;

private:
    // move/resize now both operate on "whatever is selected", with the
    // clicked note as the anchor - a single selected note is just a group of
    // one, so there's no separate single-note code path any more.
    enum class DragMode { none, move, resize, marquee };

    void clearSelection();
    void deleteSelectedNotes();

    bool isEditable() const { return editableProvider != nullptr && editableProvider(); }

    juce::Rectangle<float> noteBounds (const Note& note) const;
    bool isNearRightEdge (juce::Point<float> position, const Note& note) const;
    int findNoteAt (juce::Point<float> position) const;

    // What a Command-click at `position` would create - grid-snapped start,
    // default 1-beat length. Shared by the actual placement in mouseDown
    // and the ghost-note preview in paint(), so the preview is always
    // exactly what you'd get, never just an approximation of it.
    Note placementNoteAt (juce::Point<float> position) const;

    // Collapsed: pitch range squeezed to fit whatever height this has.
    // Expanded: fixed bigger row height, tall enough to need scrolling.
    float rowHeight() const;

    float keyStripW() const { return kKeyboardStripWidth; }

    float noteTopY (int pitch) const;
    int yToPitch (float y) const;
    double xToBeats (float x) const { return (x - keyStripW()) / kPixelsPerBeat; }

    void drawKeyboardStrip (juce::Graphics& g, float rh);

    static bool isBlackKey (int pitch);

    void commit();
    void timerCallback() override;

    std::function<std::vector<Note>()> notesProvider;
    std::function<void (std::vector<Note>)> editCallback;
    std::function<bool()> editableProvider;
    std::function<void()> laneClickHandler;
    std::function<GridSettings()> gridSettingsProvider;
    std::function<double()> playheadBeatsProvider;
    bool selectedFlag = false;
    bool expandedFlag = false;
    bool wasIdleLastTick = false;

    std::vector<Note> notes;

    // Snapshot of `notes` taken at mouse-down, giving every dragged note a
    // stable "original position" to compute this gesture's delta from on
    // each mouseDrag call - same principle as the old single-note
    // dragStartNote, just applied per-index across however many are selected.
    std::vector<Note> dragSnapshot;
    int anchorIndex = -1;
    DragMode dragMode = DragMode::none;
    juce::Point<float> dragStartPos;

    bool marqueeAdditive = false;
    juce::Rectangle<float> marqueeRect;

    // Process-wide, message-thread-only (mouse/keyboard driven, never
    // touched by the audio thread) - a plain static needs no
    // synchronization, same reasoning as `notes` itself not being atomic.
    // This is what makes copy on one track + paste on a different track work.
    static std::vector<Note> sharedClipboard;

    static constexpr int lowestNote = 36;
    static constexpr int highestNote = 96;
    static constexpr float fixedExpandedRowHeight = 16.0f;

    const juce::Colour backgroundColour { 0xfff2f0e9 };
    const juce::Colour noteColour { 0xffdb7d29 };
};

// Places a PianoRollCanvas inside a Viewport, sizing it to fill the view
// when collapsed or to its full scrollable content height when expanded.
class PianoRollView : public juce::Component
{
public:
    using Note = PianoRollCanvas::Note;

    PianoRollView();

    void setNotesProvider (std::function<std::vector<Note>()> provider) { canvas.setNotesProvider (std::move (provider)); }
    void setEditCallback (std::function<void (std::vector<Note>)> callback) { canvas.setEditCallback (std::move (callback)); }
    void setEditableProvider (std::function<bool()> provider) { canvas.setEditableProvider (std::move (provider)); }
    void setLaneClickHandler (std::function<void()> handler) { canvas.setLaneClickHandler (std::move (handler)); }
    void setGridSettingsProvider (std::function<GridSettings()> provider) { canvas.setGridSettingsProvider (std::move (provider)); }
    void setPlayheadBeatsProvider (std::function<double()> provider) { canvas.setPlayheadBeatsProvider (std::move (provider)); }
    void refreshNotes() { canvas.refreshNotes(); }
    void setSelected (bool newSelected) { canvas.setSelectedFlag (newSelected); }

    void setExpanded (bool newExpanded);

    void resized() override;

private:
    juce::Viewport viewport;
    PianoRollCanvas canvas;
    bool expandedFlag = false;
};
