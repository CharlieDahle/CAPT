#include "Metronome.h"

void Metronome::prepareToPlay (double newSampleRate)
{
    sampleRate = newSampleRate;
    clickLengthSamples = (int) (0.03 * sampleRate);
    remainingClickSamples = 0;
    nextBeatSampleTime = 0.0;
    beatCounter = 0;
    wasRunning = false;
}

void Metronome::triggerClick (bool isDownbeat)
{
    remainingClickSamples = clickLengthSamples;
    clickPhase = 0.0;

    auto frequency = isDownbeat ? 1500.0 : 1000.0;
    clickPhaseDelta = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
}

void Metronome::renderNextBlock (juce::AudioBuffer<float>& buffer, int numSamples,
                                  double elapsedSamplesAtBlockStart, bool transportRunning,
                                  double bpm, int timeSignatureNumerator)
{
    if (! transportRunning)
    {
        wasRunning = false;
        return;
    }

    if (! wasRunning)
    {
        // Transport just started (or restarted from 0) - the downbeat
        // lands exactly at the start, matching elapsedSamples resetting to
        // 0 on transport transitions in AudioEngine.
        nextBeatSampleTime = elapsedSamplesAtBlockStart;
        beatCounter = 0;
        wasRunning = true;
    }

    auto samplesPerBeat = sampleRate * 60.0 / juce::jmax (1.0, bpm);
    auto enabledNow = enabled.load();
    auto currentGain = gain.load();

    for (int i = 0; i < numSamples; ++i)
    {
        auto sampleTime = elapsedSamplesAtBlockStart + (double) i;

        if (sampleTime >= nextBeatSampleTime)
        {
            if (enabledNow)
                triggerClick (timeSignatureNumerator > 0 && (beatCounter % timeSignatureNumerator) == 0);

            ++beatCounter;
            nextBeatSampleTime += samplesPerBeat;
        }

        if (remainingClickSamples > 0)
        {
            auto envelope = (float) remainingClickSamples / (float) juce::jmax (1, clickLengthSamples);
            auto sampleValue = (float) (std::sin (clickPhase) * envelope * currentGain);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample (channel, i, sampleValue);

            clickPhase += clickPhaseDelta;
            --remainingClickSamples;
        }
    }
}
