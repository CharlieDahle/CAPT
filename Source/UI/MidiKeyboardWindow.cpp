#include "MidiKeyboardWindow.h"

MidiKeyboardWindow::Content::Content (juce::MidiKeyboardState& state)
    : keyboard (state, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // Full MIDI range so a QWERTY-triggered note is always drawn highlighted
    // regardless of octave - keyPresses aren't clamped to the visible range,
    // only what's scrolled into view is. Scroll buttons (on by default)
    // handle the rest for mouse play.
    keyboard.setAvailableRange (0, 127);
    keyboard.setLowestVisibleKey (36);
    // Explicit, though it's already JUCE's own default - pins down the
    // octave-numbering the keys themselves are labelled with (middle C =
    // "C3"), which is what shiftOctave's math below is built around.
    keyboard.setOctaveForMiddleC (3);
    keyboard.setKeyPressBaseOctave (currentOctave + 2);
    addAndMakeVisible (keyboard);

    // Buttons grab keyboard focus on click by default, which would steal it
    // away from the piano and break QWERTY playing right after clicking one
    // of these - the octave shift itself doesn't need focus, so don't take it.
    octaveDownButton.setMouseClickGrabsKeyboardFocus (false);
    octaveDownButton.onClick = [this] { shiftOctave (-1); };
    addAndMakeVisible (octaveDownButton);

    octaveUpButton.setMouseClickGrabsKeyboardFocus (false);
    octaveUpButton.onClick = [this] { shiftOctave (1); };
    addAndMakeVisible (octaveUpButton);

    octaveLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (octaveLabel);
    updateOctaveLabel();

    // QWERTY key presses go to whichever component has keyboard focus - grab
    // it immediately so typing works without an extra click first.
    keyboard.grabKeyboardFocus();
}

bool MidiKeyboardWindow::Content::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress ('k', juce::ModifierKeys::commandModifier, 0))
        return false;

    if (onToggleShortcut)
        onToggleShortcut();

    return true;
}

void MidiKeyboardWindow::Content::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (28);

    octaveDownButton.setBounds (toolbar.removeFromLeft (80).reduced (2));
    octaveUpButton.setBounds (toolbar.removeFromRight (80).reduced (2));
    octaveLabel.setBounds (toolbar);

    keyboard.setBounds (area);
}

void MidiKeyboardWindow::Content::shiftOctave (int delta)
{
    // currentOctave matches the octave numbers JUCE itself prints on the
    // piano keys (setOctaveForMiddleC(3) above, so middle C = MIDI note 60
    // is labelled "C3") - not JUCE's raw keyPressBaseOctave, which is a
    // different number: base pitch = 12 * keyPressBaseOctave, and that
    // pitch's printed label = keyPressBaseOctave - 5 + octaveForMiddleC.
    // Solving for keyPressBaseOctave so the *label* equals currentOctave:
    // keyPressBaseOctave = currentOctave + 5 - octaveForMiddleC(3) =
    // currentOctave + 2.
    //
    // The awsedftgyhujkolp; row spans 16 semitones from the base note, so
    // C7 (keyPressBaseOctave 9, note 108) is the highest that still fits
    // under note 128; C-2 (keyPressBaseOctave 0, note 0) is the lowest.
    currentOctave = juce::jlimit (-2, 7, currentOctave + delta);
    auto keyPressBaseOctave = currentOctave + 2;
    auto basePitch = 12 * keyPressBaseOctave;
    keyboard.setKeyPressBaseOctave (keyPressBaseOctave);
    keyboard.setLowestVisibleKey (juce::jlimit (0, 127, basePitch - 12));
    updateOctaveLabel();
}

void MidiKeyboardWindow::Content::updateOctaveLabel()
{
    octaveLabel.setText ("Octave: C" + juce::String (currentOctave), juce::dontSendNotification);
}

MidiKeyboardWindow::MidiKeyboardWindow (juce::MidiKeyboardState& state)
    : juce::DocumentWindow ("MIDI Keyboard", juce::Colours::darkgrey, juce::DocumentWindow::closeButton),
      content (state)
{
    setUsingNativeTitleBar (true);
    setResizable (true, false);
    setContentNonOwned (&content, true);
    centreWithSize (600, 190);
    setVisible (true);

    // Cmd/Ctrl+K pressed while this window already has focus means "close",
    // as opposed to "open" or "focus", which are handled by whoever else
    // catches the shortcut while this window doesn't have focus.
    content.onToggleShortcut = [this] { closeButtonPressed(); };
}

void MidiKeyboardWindow::closeButtonPressed()
{
    setVisible (false);

    if (onCloseRequested)
        onCloseRequested();
}

void MidiKeyboardWindow::focusKeyboard()
{
    toFront (true);
    content.focusKeyboard();
}
