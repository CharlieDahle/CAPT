#pragma once

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
};

struct Tempo
{
    double bpm = 120.0;
    TimeSignature timeSignature;

    double secondsPerBeat() const { return 60.0 / bpm; }
};

// The only place seconds/samples (real time) and beats (musical time) meet -
// always computed fresh from whatever the current tempo is, never cached,
// so a live tempo change takes effect immediately.
double secondsToBeats (double seconds, const Tempo& tempo);
double beatsToSeconds (double beats, const Tempo& tempo);

double nearestGridBeats (double beats, int stepsPerBeat);

// Beats-space grid settings for drawing/quantizing - deliberately has no
// tempo/bpm in it: grid line pixel positions must never depend on tempo,
// only on subdivision (stepsPerBeat) and bar length (timeSignature.numerator).
struct GridSettings
{
    TimeSignature timeSignature;
    int stepsPerBeat = 4;
};
