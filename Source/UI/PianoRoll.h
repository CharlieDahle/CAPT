#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"

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
        double startSeconds = 0.0;
        double endSeconds = 0.5;
        float velocity = 0.8f;
    };

    PianoRollCanvas();

    void setNotesProvider (std::function<std::vector<Note>()> provider) { notesProvider = std::move (provider); }
    void setEditCallback (std::function<void (std::vector<Note>)> callback) { editCallback = std::move (callback); }
    void setEditableProvider (std::function<bool()> provider) { editableProvider = std::move (provider); }

    void setLaneClickHandler (std::function<void()> handler) { laneClickHandler = std::move (handler); }

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

private:
    enum class DragMode { move, resize };

    bool isEditable() const { return editableProvider != nullptr && editableProvider(); }

    juce::Rectangle<float> noteBounds (const Note& note) const;
    bool isNearRightEdge (juce::Point<float> position, const Note& note) const;
    int findNoteAt (juce::Point<float> position) const;

    // Collapsed: pitch range squeezed to fit whatever height this has.
    // Expanded: fixed bigger row height, tall enough to need scrolling.
    float rowHeight() const;

    float keyStripW() const { return kKeyboardStripWidth; }

    float noteTopY (int pitch) const;
    int yToPitch (float y) const;
    double xToSeconds (float x) const { return (x - keyStripW()) / kPixelsPerSecond; }

    void drawKeyboardStrip (juce::Graphics& g, float rh);

    static bool isBlackKey (int pitch);

    void commit();
    void timerCallback() override;

    std::function<std::vector<Note>()> notesProvider;
    std::function<void (std::vector<Note>)> editCallback;
    std::function<bool()> editableProvider;
    std::function<void()> laneClickHandler;
    bool selectedFlag = false;
    bool expandedFlag = false;
    bool wasIdleLastTick = false;

    std::vector<Note> notes;
    int draggingIndex = -1;
    DragMode dragMode = DragMode::move;
    Note dragStartNote;
    juce::Point<float> dragStartPos;

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
    void setSelected (bool newSelected) { canvas.setSelectedFlag (newSelected); }

    void setExpanded (bool newExpanded);

    void resized() override;

private:
    juce::Viewport viewport;
    PianoRollCanvas canvas;
    bool expandedFlag = false;
};
