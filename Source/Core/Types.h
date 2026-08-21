#pragma once

enum class TransportState { Idle, Recording, Playing };
enum class TrackType { Midi, Audio };
enum class OscillatorWaveform { Sine, Saw, Square, Triangle };

// Shared by the piano roll, the grid, and the playhead. Fixed regardless of
// tempo - beats are the canonical musical-time unit, so bar 4 always sits
// at the same x; only the playhead's speed across it depends on BPM.
// Numerically equal to the old kPixelsPerSecond's default look at 120bpm
// (60px/s * 0.5s/beat = 30px/beat) so nothing suddenly looks different at
// the default tempo.
constexpr double kPixelsPerBeat = 30.0;

// Reserved unconditionally, even collapsed where no keys are drawn, so
// time-zero sits at the same x in every track state - the playhead is drawn
// once globally at a single shared x.
constexpr float kKeyboardStripWidth = 30.0f;

// Height of the name-label strip every track reserves at its top (see
// TrackBase::resized/paint) - content (piano roll, grid lines, etc.) starts
// below this.
constexpr float kTrackHeaderHeight = 20.0f;

struct RecordedNoteEvent
{
    double beatPosition = 0.0;
    int noteNumber = 0;
    float velocity = 0.0f;
    bool isNoteOn = false;
};
