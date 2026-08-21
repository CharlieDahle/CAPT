#include "PianoRoll.h"
#include "GridPainter.h"

PianoRollCanvas::PianoRollCanvas()
{
    startTimerHz (15);
}

void PianoRollCanvas::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto rh = rowHeight();
    auto stripW = keyStripW();

    if (gridSettingsProvider != nullptr)
        GridPainter::paintVerticalGridLines (g, getLocalBounds().toFloat(), stripW, gridSettingsProvider(),
                                              juce::Colours::black.withAlpha (0.3f), juce::Colours::black.withAlpha (0.08f));

    if (expandedFlag)
    {
        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            auto y = noteTopY (pitch);

            if (isBlackKey (pitch))
            {
                g.setColour (juce::Colours::black.withAlpha (0.06f));
                g.fillRect (juce::Rectangle<float> (stripW, y, (float) getWidth() - stripW, rh));
            }

            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.drawHorizontalLine ((int) y, stripW, (float) getWidth());
        }

        drawKeyboardStrip (g, rh);

        if (! isEditable())
        {
            g.setColour (juce::Colours::darkred.withAlpha (0.15f));
            g.fillRect (stripW, 0.0f, (float) getWidth() - stripW, (float) getHeight());
        }
    }
    else
    {
        // Same reserved gutter as the expanded view, just without keys.
        g.setColour (juce::Colours::darkslategrey);
        g.fillRect (0.0f, 0.0f, stripW, (float) getHeight());
        g.setColour (juce::Colours::darkgrey.withAlpha (0.4f));
        g.drawVerticalLine ((int) stripW, 0.0f, (float) getHeight());
    }

    for (auto& note : notes)
    {
        auto bounds = noteBounds (note);
        g.setColour (noteColour.withAlpha (juce::jlimit (0.5f, 1.0f, note.velocity)));
        g.fillRect (bounds);

        if (expandedFlag)
        {
            g.setColour (noteColour.darker (0.4f));
            g.drawRect (bounds, 1.0f);
        }
    }
}

void PianoRollCanvas::mouseDown (const juce::MouseEvent& e)
{
    if (! (selectedFlag && expandedFlag))
    {
        if (laneClickHandler)
            laneClickHandler();
        return;
    }

    if (! isEditable() || e.position.x < keyStripW())
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
        newNote.startBeats = juce::jmax (0.0, xToBeats (e.position.x));
        newNote.endBeats = newNote.startBeats + 1.0;
        notes.push_back (newNote);
        draggingIndex = (int) notes.size() - 1;
        dragStartNote = newNote;
        dragMode = DragMode::resize;
    }

    dragStartPos = e.position;
    repaint();
}

void PianoRollCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingIndex < 0)
        return;

    auto& note = notes[(size_t) draggingIndex];
    auto deltaBeats = (e.position.x - dragStartPos.x) / kPixelsPerBeat;

    if (dragMode == DragMode::move)
    {
        auto length = dragStartNote.endBeats - dragStartNote.startBeats;
        auto rawStart = juce::jmax (0.0, dragStartNote.startBeats + deltaBeats);
        note.startBeats = gridSettingsProvider != nullptr
                             ? nearestGridBeats (rawStart, gridSettingsProvider().stepsPerBeat)
                             : rawStart;
        note.endBeats = note.startBeats + length;
        note.pitch = yToPitch (e.position.y);
    }
    else
    {
        auto rawEnd = juce::jmax (note.startBeats + 0.05, dragStartNote.endBeats + deltaBeats);
        note.endBeats = gridSettingsProvider != nullptr
                           ? juce::jmax (note.startBeats + 0.05, nearestGridBeats (rawEnd, gridSettingsProvider().stepsPerBeat))
                           : rawEnd;
    }

    repaint();
}

void PianoRollCanvas::mouseUp (const juce::MouseEvent&)
{
    if (draggingIndex < 0)
        return;

    draggingIndex = -1;
    commit();
}

void PianoRollCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (selectedFlag && expandedFlag && isEditable())
    {
        auto hitIndex = findNoteAt (e.position);

        if (hitIndex >= 0)
        {
            notes[(size_t) hitIndex].velocity = juce::jlimit (0.05f, 1.0f, notes[(size_t) hitIndex].velocity + wheel.deltaY * 0.5f);
            commit();
            return;
        }
    }

    // Fall through so the event reaches the enclosing Viewport and it
    // can actually scroll.
    Component::mouseWheelMove (e, wheel);
}

juce::Rectangle<float> PianoRollCanvas::noteBounds (const Note& note) const
{
    auto x = keyStripW() + (float) (note.startBeats * kPixelsPerBeat);
    auto width = juce::jmax (2.0f, (float) ((note.endBeats - note.startBeats) * kPixelsPerBeat));
    auto rh = rowHeight();
    return { x, noteTopY (note.pitch) + 0.5f, width, juce::jmax (2.0f, rh - 1.0f) };
}

bool PianoRollCanvas::isNearRightEdge (juce::Point<float> position, const Note& note) const
{
    auto bounds = noteBounds (note);
    return std::abs (position.x - bounds.getRight()) < 6.0f && bounds.getY() - 4.0f <= position.y
           && position.y <= bounds.getBottom() + 4.0f;
}

int PianoRollCanvas::findNoteAt (juce::Point<float> position) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteBounds (notes[(size_t) i]).expanded (0.0f, 3.0f).contains (position))
            return i;

    return -1;
}

float PianoRollCanvas::rowHeight() const
{
    if (expandedFlag)
        return fixedExpandedRowHeight;

    return (float) juce::jmax (1, getHeight()) / (float) (highestNote - lowestNote + 1);
}

float PianoRollCanvas::noteTopY (int pitch) const
{
    auto clamped = juce::jlimit (lowestNote, highestNote, pitch);
    return (float) (highestNote - clamped) * rowHeight();
}

int PianoRollCanvas::yToPitch (float y) const
{
    auto row = (int) (y / rowHeight());
    return juce::jlimit (lowestNote, highestNote, highestNote - row);
}

void PianoRollCanvas::drawKeyboardStrip (juce::Graphics& g, float rh)
{
    auto stripW = keyStripW();

    for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
    {
        juce::Rectangle<float> keyArea (0.0f, noteTopY (pitch), stripW, rh);

        g.setColour (isBlackKey (pitch) ? juce::Colours::black : juce::Colours::white);
        g.fillRect (keyArea);
        g.setColour (juce::Colours::grey);
        g.drawRect (keyArea, 0.5f);

        if (pitch % 12 == 0)
        {
            g.setColour (juce::Colours::grey);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("C" + juce::String (pitch / 12 - 1), keyArea.reduced (2.0f, 0.0f),
                        juce::Justification::centredLeft);
        }
    }

    g.setColour (juce::Colours::darkgrey);
    g.drawVerticalLine ((int) stripW, 0.0f, (float) getHeight());
}

bool PianoRollCanvas::isBlackKey (int pitch)
{
    auto m = pitch % 12;
    return m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
}

void PianoRollCanvas::commit()
{
    if (editCallback != nullptr)
        editCallback (notes);

    repaint();
}

void PianoRollCanvas::timerCallback()
{
    // Skip entirely while hidden/minimized - nothing to repaint.
    if (! isShowing())
        return;

    // Idle recordedEvents only change via an explicit edit, which
    // already repaints itself through commit() - so skip the recurring
    // pull while idle, except once on the transition into idle so the
    // tail end of a recording doesn't linger stale.
    auto isIdleNow = editableProvider != nullptr && editableProvider();

    if (isIdleNow && wasIdleLastTick)
        return;

    wasIdleLastTick = isIdleNow;

    // Don't clobber an in-progress drag with a stale snapshot.
    if (draggingIndex < 0 && notesProvider != nullptr)
        notes = notesProvider();

    repaint();
}

PianoRollView::PianoRollView()
{
    viewport.setViewedComponent (&canvas, false);

    // No per-track horizontal scrolling - one shared timeline for the
    // whole app; this viewport only scrolls vertically, through pitch.
    viewport.setScrollBarsShown (true, false);

    addAndMakeVisible (viewport);
}

void PianoRollView::setExpanded (bool newExpanded)
{
    expandedFlag = newExpanded;
    canvas.setExpandedFlag (newExpanded);
    resized();

    // Land in the middle of the range rather than at the top every time.
    if (expandedFlag)
        viewport.setViewPosition (0, juce::jmax (0, canvas.getHeight() / 2 - viewport.getHeight() / 2));
}

void PianoRollView::resized()
{
    viewport.setBounds (getLocalBounds());

    if (expandedFlag)
        canvas.setSize (juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness()),
                         canvas.totalHeightNeeded());
    else
        canvas.setSize (viewport.getWidth(), viewport.getHeight());
}
