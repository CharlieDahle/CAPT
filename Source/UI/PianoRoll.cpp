#include "PianoRoll.h"
#include "GridPainter.h"

std::vector<PianoRollCanvas::Note> PianoRollCanvas::sharedClipboard;

PianoRollCanvas::PianoRollCanvas()
{
    startTimerHz (15);

    // Needed to receive Delete/Cmd+C/X/V - grabbed on mouseDown once we're
    // actually in editing mode, not here (nothing to focus yet at
    // construction). See MidiKeyboardWindow::Content for the same pattern.
    setWantsKeyboardFocus (true);
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
            // Selection only means anything while expanded - collapsed
            // thumbnails aren't interactive, so nothing can be selected there.
            g.setColour (note.selected ? juce::Colours::white : noteColour.darker (0.4f));
            g.drawRect (bounds, note.selected ? 2.0f : 1.0f);
        }
    }

    if (dragMode == DragMode::marquee)
    {
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRect (marqueeRect);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawRect (marqueeRect, 1.0f);
    }

    // Ghost preview of what a Command-click would place right now - exactly
    // placementNoteAt() of the current mouse position, so it's never just an
    // approximation of the real thing. Suppressed mid-drag (dragMode != none)
    // since that's already a note being placed/moved/resized, not a preview.
    if (expandedFlag && selectedFlag && isEditable() && dragMode == DragMode::none
        && juce::ModifierKeys::getCurrentModifiers().isCommandDown() && isMouseOver())
    {
        auto mousePos = getMouseXYRelative().toFloat();

        if (mousePos.x >= keyStripW())
        {
            auto ghostBounds = noteBounds (placementNoteAt (mousePos));
            g.setColour (noteColour.withAlpha (0.35f));
            g.fillRect (ghostBounds);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawRect (ghostBounds, 1.5f);
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

    // Only reachable once we're actually in editing mode - this is what
    // lets Delete/Cmd+C/X/V reach us instead of bubbling straight past.
    grabKeyboardFocus();

    if (e.mods.isRightButtonDown())
    {
        auto hitIndex = findNoteAt (e.position);

        if (hitIndex >= 0)
        {
            if (notes[(size_t) hitIndex].selected)
                deleteSelectedNotes();
            else
            {
                notes.erase (notes.begin() + hitIndex);
                commit();
            }
        }

        return;
    }

    if (e.mods.isCommandDown())
    {
        auto newNote = placementNoteAt (e.position);
        newNote.selected = true;
        clearSelection();
        notes.push_back (newNote);

        anchorIndex = (int) notes.size() - 1;
        dragSnapshot = notes;
        dragStartPos = e.position;
        dragMode = DragMode::resize;
        repaint();
        return;
    }

    if (e.mods.isShiftDown())
    {
        auto hitIndex = findNoteAt (e.position);

        if (hitIndex >= 0)
        {
            // Toggle only - no drag starts on this click. Dragging the
            // resulting selection is a separate, subsequent click-drag.
            notes[(size_t) hitIndex].selected = ! notes[(size_t) hitIndex].selected;
            dragMode = DragMode::none;
        }
        else
        {
            marqueeAdditive = true;
            dragSnapshot = notes;
            dragStartPos = e.position;
            marqueeRect = { e.position, e.position };
            dragMode = DragMode::marquee;
        }

        repaint();
        return;
    }

    auto hitIndex = findNoteAt (e.position);

    if (hitIndex >= 0)
    {
        // Clicking an already-selected note preserves the rest of the
        // selection, so grabbing any member of a group drags all of it.
        // Clicking an unselected one replaces the selection with just it.
        if (! notes[(size_t) hitIndex].selected)
        {
            clearSelection();
            notes[(size_t) hitIndex].selected = true;
        }

        anchorIndex = hitIndex;
        dragSnapshot = notes;
        dragStartPos = e.position;
        dragMode = isNearRightEdge (e.position, notes[(size_t) hitIndex]) ? DragMode::resize : DragMode::move;
    }
    else
    {
        clearSelection();
        dragSnapshot = notes;
        dragStartPos = e.position;
        marqueeAdditive = false;
        marqueeRect = { e.position, e.position };
        dragMode = DragMode::marquee;
    }

    repaint();
}

void PianoRollCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    if (dragMode == DragMode::marquee)
    {
        marqueeRect = juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (dragStartPos.x, e.position.x), juce::jmin (dragStartPos.y, e.position.y),
            juce::jmax (dragStartPos.x, e.position.x), juce::jmax (dragStartPos.y, e.position.y));

        for (size_t i = 0; i < notes.size() && i < dragSnapshot.size(); ++i)
        {
            auto caughtByBox = noteBounds (dragSnapshot[i]).intersects (marqueeRect);
            notes[i].selected = (marqueeAdditive && dragSnapshot[i].selected) || caughtByBox;
        }

        repaint();
        return;
    }

    if (anchorIndex < 0 || anchorIndex >= (int) dragSnapshot.size())
        return;

    // Anchor's raw movement is what gets snapped to the grid; every other
    // selected note is then shifted by that same *snapped* delta, so the
    // group moves/resizes together without each note re-snapping on its own
    // and drifting out of relative alignment.
    auto& anchorStart = dragSnapshot[(size_t) anchorIndex];
    auto deltaBeats = (e.position.x - dragStartPos.x) / kPixelsPerBeat;

    if (dragMode == DragMode::move)
    {
        auto rawStart = juce::jmax (0.0, anchorStart.startBeats + deltaBeats);
        auto snappedAnchorStart = gridSettingsProvider != nullptr
                                     ? nearestGridBeats (rawStart, gridSettingsProvider().stepsPerBeat)
                                     : rawStart;
        auto actualDeltaBeats = snappedAnchorStart - anchorStart.startBeats;
        auto deltaPitch = yToPitch (e.position.y) - yToPitch (dragStartPos.y);

        for (size_t i = 0; i < notes.size() && i < dragSnapshot.size(); ++i)
        {
            if (! dragSnapshot[i].selected)
                continue;

            auto length = dragSnapshot[i].endBeats - dragSnapshot[i].startBeats;
            notes[i].startBeats = juce::jmax (0.0, dragSnapshot[i].startBeats + actualDeltaBeats);
            notes[i].endBeats = notes[i].startBeats + length;
            notes[i].pitch = juce::jlimit (lowestNote, highestNote, dragSnapshot[i].pitch + deltaPitch);
        }
    }
    else if (dragMode == DragMode::resize)
    {
        auto rawEnd = juce::jmax (anchorStart.startBeats + 0.05, anchorStart.endBeats + deltaBeats);
        auto snappedAnchorEnd = gridSettingsProvider != nullptr
                                   ? juce::jmax (anchorStart.startBeats + 0.05, nearestGridBeats (rawEnd, gridSettingsProvider().stepsPerBeat))
                                   : rawEnd;
        auto actualDeltaBeats = snappedAnchorEnd - anchorStart.endBeats;

        // Each note clamps to its own minimum length independently - a very
        // short note in the group can hit its floor before the others do,
        // without holding the rest of the stretch back.
        for (size_t i = 0; i < notes.size() && i < dragSnapshot.size(); ++i)
        {
            if (! dragSnapshot[i].selected)
                continue;

            notes[i].endBeats = juce::jmax (dragSnapshot[i].startBeats + 0.05, dragSnapshot[i].endBeats + actualDeltaBeats);
        }
    }

    repaint();
}

void PianoRollCanvas::mouseUp (const juce::MouseEvent&)
{
    if (dragMode == DragMode::move || dragMode == DragMode::resize)
        commit();
    else if (dragMode == DragMode::marquee)
        repaint();

    dragMode = DragMode::none;
    anchorIndex = -1;
}

void PianoRollCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (selectedFlag && expandedFlag && isEditable())
    {
        auto hitIndex = findNoteAt (e.position);

        if (hitIndex >= 0)
        {
            if (notes[(size_t) hitIndex].selected)
            {
                for (auto& note : notes)
                    if (note.selected)
                        note.velocity = juce::jlimit (0.05f, 1.0f, note.velocity + wheel.deltaY * 0.5f);
            }
            else
            {
                auto& note = notes[(size_t) hitIndex];
                note.velocity = juce::jlimit (0.05f, 1.0f, note.velocity + wheel.deltaY * 0.5f);
            }

            commit();
            return;
        }
    }

    // Fall through so the event reaches the enclosing Viewport and it
    // can actually scroll.
    Component::mouseWheelMove (e, wheel);
}

bool PianoRollCanvas::keyPressed (const juce::KeyPress& key)
{
    if (! (isEditable() && selectedFlag && expandedFlag))
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNotes();
        return true;
    }

    if (key == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0))
    {
        sharedClipboard.clear();

        for (auto& note : notes)
            if (note.selected)
                sharedClipboard.push_back (note);

        return true;
    }

    if (key == juce::KeyPress ('x', juce::ModifierKeys::commandModifier, 0))
    {
        sharedClipboard.clear();

        for (auto& note : notes)
            if (note.selected)
                sharedClipboard.push_back (note);

        deleteSelectedNotes();
        return true;
    }

    if (key == juce::KeyPress ('v', juce::ModifierKeys::commandModifier, 0))
    {
        if (sharedClipboard.empty())
            return true;

        auto anchorBeats = sharedClipboard.front().startBeats;

        for (auto& note : sharedClipboard)
            anchorBeats = juce::jmin (anchorBeats, note.startBeats);

        auto playheadBeats = playheadBeatsProvider != nullptr ? playheadBeatsProvider() : 0.0;

        clearSelection();

        for (auto note : sharedClipboard)
        {
            auto length = note.endBeats - note.startBeats;
            note.startBeats = juce::jmax (0.0, playheadBeats + (note.startBeats - anchorBeats));
            note.endBeats = note.startBeats + length;
            note.selected = true;
            notes.push_back (note);
        }

        commit();
        return true;
    }

    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        for (auto& note : notes)
            note.selected = true;

        repaint();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        clearSelection();
        repaint();
        return true;
    }

    return false;
}

juce::MouseCursor PianoRollCanvas::getMouseCursor()
{
    if (expandedFlag && selectedFlag && isEditable() && juce::ModifierKeys::getCurrentModifiers().isCommandDown())
        return juce::MouseCursor::CopyingCursor;

    return juce::MouseCursor::NormalCursor;
}

PianoRollCanvas::Note PianoRollCanvas::placementNoteAt (juce::Point<float> position) const
{
    Note note;
    note.pitch = yToPitch (position.y);

    auto rawStart = juce::jmax (0.0, xToBeats (position.x));
    auto stepsPerBeat = gridSettingsProvider != nullptr ? gridSettingsProvider().stepsPerBeat : 1;
    note.startBeats = nearestGridBeats (rawStart, stepsPerBeat);
    note.endBeats = note.startBeats + 1.0;
    return note;
}

void PianoRollCanvas::clearSelection()
{
    for (auto& note : notes)
        note.selected = false;
}

void PianoRollCanvas::deleteSelectedNotes()
{
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.selected; }), notes.end());
    commit();
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

    // Modifier-key-only changes (Cmd pressed/released with the mouse
    // stationary) don't fire a mouseMove on their own - piggyback on this
    // existing 15Hz timer to re-check getMouseCursor() instead of waiting
    // for the mouse to move before the placement-cursor hint appears.
    updateMouseCursor();

    // Idle recordedEvents only change via an explicit edit, which already
    // repaints itself through commit() - so skip the recurring pull while
    // idle, except once on the transition into idle so the tail end of a
    // recording doesn't linger stale. The repaint below still always runs
    // though (cheap at 15Hz on one canvas) - it's what lets the ghost-note
    // preview and placement cursor react to Cmd being pressed/released with
    // a perfectly still mouse, not just to actual mouse movement.
    auto isIdleNow = editableProvider != nullptr && editableProvider();
    auto justBecameIdle = isIdleNow && ! wasIdleLastTick;
    wasIdleLastTick = isIdleNow;

    // Don't clobber an in-progress drag with a stale snapshot.
    if ((! isIdleNow || justBecameIdle) && dragMode == DragMode::none && notesProvider != nullptr)
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
