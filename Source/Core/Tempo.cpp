#include "Tempo.h"
#include <algorithm>
#include <cmath>

double secondsToBeats (double seconds, const Tempo& tempo)
{
    auto secondsPerBeat = tempo.secondsPerBeat();
    return secondsPerBeat > 0.0 ? seconds / secondsPerBeat : 0.0;
}

double beatsToSeconds (double beats, const Tempo& tempo)
{
    return beats * tempo.secondsPerBeat();
}

double nearestGridBeats (double beats, int stepsPerBeat)
{
    auto step = 1.0 / (double) std::max (1, stepsPerBeat);
    return std::round (beats / step) * step;
}
