#include "AudioEngine.h"
#include "../MicrophonePermission.h"
#include <cstdio>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::addTrack (TrackAudioSource* track)
{
    tracks.push_back (track);
}

void AudioEngine::start()
{
    // JUCE's CoreAudio backend opens the input device without calling
    // AVFoundation's authorization API, so the mic permission prompt
    // doesn't reliably appear on its own - request it explicitly.
    requestMicrophonePermission ([] (bool /*granted*/) {});

    deviceManager.initialise (1, 2, nullptr, true);

    // Without an explicit request, the device keeps whatever buffer size
    // CoreAudio's shared HAL setting currently has - can be as small as 16
    // samples if left over from another low-latency audio app, too small
    // for the OS to service reliably and a source of constant crackle
    // unrelated to anything this app does.
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = 512;
    deviceManager.setAudioDeviceSetup (setup, true);

    deviceManager.addAudioCallback (this);
    startTimerHz (1);
}

void AudioEngine::setTempo (Tempo tempo)
{
    tempoBpm.store (tempo.bpm);
    tempoNumerator.store (tempo.timeSignature.numerator);
    tempoDenominator.store (tempo.timeSignature.denominator);
}

Tempo AudioEngine::getTempo() const
{
    Tempo tempo;
    tempo.bpm = tempoBpm.load();
    tempo.timeSignature.numerator = tempoNumerator.load();
    tempo.timeSignature.denominator = tempoDenominator.load();
    return tempo;
}

void AudioEngine::replaceTrackAt (int index, TrackAudioSource* newTrack)
{
    const juce::ScopedLock lock (tracksLock);
    tracks[(size_t) index] = newTrack;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    currentSampleRate = device->getCurrentSampleRate();

    for (auto* track : tracks)
        track->prepareToPlay (currentSampleRate);

    scratchBuffer.setSize (2, device->getCurrentBufferSizeSamples());
    simulatedInputScratch.setSize (1, device->getCurrentBufferSizeSamples());
    metronome.prepareToPlay (currentSampleRate);

    expectedCallbackIntervalMs = 1000.0 * (double) device->getCurrentBufferSizeSamples() / currentSampleRate;

    std::printf ("[audio-diag] device \"%s\" | sampleRate %.0fHz | bufferSize %d samples | inputs %d | outputs %d\n",
                 device->getName().toRawUTF8(), currentSampleRate, device->getCurrentBufferSizeSamples(),
                 device->getActiveInputChannels().countNumberOfSetBits(),
                 device->getActiveOutputChannels().countNumberOfSetBits());
    std::fflush (stdout);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                                      float* const* outputChannelData, int numOutputChannels,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    // Diagnostics only - real-time safe (no locks/allocation/I-O here).
    // Gap between callback starts catches the OS scheduling this thread
    // late; duration catches our own code being slow.
    auto callbackStart = std::chrono::steady_clock::now();

    if (havePreviousCallbackStart)
    {
        auto gapMs = std::chrono::duration<double, std::milli> (callbackStart - previousCallbackStart).count();
        auto prevMaxGap = maxCallbackGapMs.load (std::memory_order_relaxed);
        if (gapMs > prevMaxGap)
            maxCallbackGapMs.store (gapMs, std::memory_order_relaxed);
    }

    previousCallbackStart = callbackStart;
    havePreviousCallbackStart = true;

    juce::AudioBuffer<float> outputBuffer (outputChannelData, numOutputChannels, numSamples);
    outputBuffer.clear();

    auto usingSimulatedInput = audioInputSimulator.isActive();

    if (usingSimulatedInput)
        audioInputSimulator.fillNextBlock (simulatedInputScratch.getWritePointer (0), numSamples);

    auto inputBuffer = usingSimulatedInput
                          ? juce::AudioBuffer<const float> (simulatedInputScratch.getArrayOfReadPointers(), 1, numSamples)
                          : juce::AudioBuffer<const float> (inputChannelData, numInputChannels, numSamples);

    auto desiredState = requestedState.load();
    auto transitioned = (desiredState != currentState);
    auto elapsedNow = elapsedSamples.load();

    if (transitioned)
    {
        elapsedNow = 0.0;
        currentState = desiredState;
    }

    auto bpm = tempoBpm.load();

    {
        // Tracks can be replaced from the message thread while this runs
        // (type toggle, load), so guard the vector itself.
        const juce::ScopedLock lock (tracksLock);

        bool anySoloed = false;
        for (auto* track : tracks)
            if (track->isSoloed())
                anySoloed = true;

        for (auto* track : tracks)
            mixTrackIntoOutput (*track, outputBuffer, numSamples, currentState, elapsedNow, bpm, &inputBuffer, anySoloed);
    }

    metronome.renderNextBlock (outputBuffer, numSamples, elapsedNow, currentState != TransportState::Idle,
                                bpm, tempoNumerator.load());

    if (currentState != TransportState::Idle)
        elapsedSamples.store (elapsedNow + numSamples);
    else
        elapsedSamples.store (0.0);

    auto durationMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - callbackStart).count();
    auto prevMaxDuration = maxCallbackDurationMs.load (std::memory_order_relaxed);
    if (durationMs > prevMaxDuration)
        maxCallbackDurationMs.store (durationMs, std::memory_order_relaxed);
}

void AudioEngine::mixTrackIntoOutput (TrackAudioSource& track, juce::AudioBuffer<float>& outputBuffer, int numSamples,
                                       TransportState globalState, double transportElapsedSamples, double bpm,
                                       const juce::AudioBuffer<const float>* inputBuffer, bool anySoloed)
{
    scratchBuffer.clear (0, numSamples);
    track.renderNextBlock (scratchBuffer, 0, numSamples, globalState, transportElapsedSamples, bpm, inputBuffer);

    // Tapped pre-mute/pre-gain, so the visualiser shows what the track is
    // actually producing even while muted (useful while sound-designing).
    if (&track == visualiserTrack.load (std::memory_order_relaxed))
        if (auto* tap = visualiserTap.load (std::memory_order_relaxed))
            tap->pushBuffer (scratchBuffer);

    auto audible = ! track.isMuted() && (! anySoloed || track.isSoloed());
    auto gain = audible ? track.getVolume() : 0.0f;

    for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
        outputBuffer.addFrom (channel, 0, scratchBuffer, channel, 0, numSamples, gain);
}

void AudioEngine::timerCallback()
{
    // Diagnostic: once a second, print the worst audio-callback gap and
    // duration seen since the last print, so we can tell whether crackling
    // is the OS failing to schedule the audio thread on time (large gap,
    // small duration) or our own code being slow (large duration). This
    // runs on the message thread, never the audio thread, so printing here
    // is safe.
    auto gapMs = maxCallbackGapMs.exchange (0.0, std::memory_order_relaxed);
    auto durationMs = maxCallbackDurationMs.exchange (0.0, std::memory_order_relaxed);

    std::printf ("[audio-diag] expected interval %.2fms | worst gap %.2fms | worst duration %.2fms\n",
                 expectedCallbackIntervalMs, gapMs, durationMs);
    std::fflush (stdout);
}
