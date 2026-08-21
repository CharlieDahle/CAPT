#pragma once

enum class TransportState { Idle, Recording, Playing };
enum class TrackType { Midi, Audio };

// Shared by the piano roll and the playhead so a note lines up with the
// playhead when it gets there.
constexpr double kPixelsPerSecond = 60.0;

// Reserved unconditionally, even collapsed where no keys are drawn, so
// time-zero sits at the same x in every track state - the playhead is drawn
// once globally at a single shared x.
constexpr float kKeyboardStripWidth = 30.0f;

struct RecordedNoteEvent
{
    double timeStampSamples = 0.0;
    int noteNumber = 0;
    float velocity = 0.0f;
    bool isNoteOn = false;
};
