#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// A movable, closable native window wrapping JUCE's built-in on-screen piano
// (juce::MidiKeyboardComponent) - lets the computer keyboard/mouse simulate
// MIDI input via the shared MidiKeyboardState passed in, without owning that
// state itself (the caller wires it up to whatever should receive notes).
class MidiKeyboardWindow : public juce::DocumentWindow
{
public:
    explicit MidiKeyboardWindow (juce::MidiKeyboardState& state);

    // DocumentWindow's default close behaviour deletes nothing on its own -
    // this just hides the window and notifies the owner so it can update
    // its toggle button state.
    std::function<void()> onCloseRequested;

    void closeButtonPressed() override;

    // Brings the popup to the front and moves keyboard focus onto the piano
    // itself, so QWERTY playing works immediately - used when the Cmd/Ctrl+K
    // shortcut fires while this window is open but some other window (e.g.
    // the main one) currently has focus.
    void focusKeyboard();

private:
    // Groups the keyboard with an octave toolbar above it, since
    // DocumentWindow only ever takes a single content component.
    class Content : public juce::Component
    {
    public:
        explicit Content (juce::MidiKeyboardState& state);

        // Fired when Cmd/Ctrl+K is pressed while this window (not some other
        // window) has focus - the only case a keypress here can mean "close"
        // rather than "open" or "focus", since those are handled wherever
        // else the shortcut was actually received.
        std::function<void()> onToggleShortcut;

        void focusKeyboard() { keyboard.grabKeyboardFocus(); }

        void resized() override;
        bool keyPressed (const juce::KeyPress& key) override;

    private:
        void shiftOctave (int delta);
        void updateOctaveLabel();

        juce::MidiKeyboardComponent keyboard;
        juce::TextButton octaveDownButton { "Octave -" }, octaveUpButton { "Octave +" };
        juce::Label octaveLabel;

        // Matches the octave numbers JUCE prints on the keys themselves
        // (see shiftOctave) - 3 means 'a' plays the note labelled C3.
        int currentOctave = 3;
    };

    Content content;
};
