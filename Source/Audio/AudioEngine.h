#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "../Core/Tempo.h"
#include "../Tracks/TrackAudioSource.h"
#include "../Dev/AudioInputSimulator.h"
#include "Metronome.h"
#include <chrono>

// Owns the audio device and the global transport - the real-time side of
// the app, with no UI dependencies. Tracks are owned by whoever creates them
// (MainComponent, since they're also juce::Components needing a place in the
// component tree) - AudioEngine only holds non-owning TrackAudioSource
// pointers, so it has zero dependency on the GUI toolkit despite the
// concrete tracks being Components too.
//
// Usage: addTrack() for each starting track, then start() once they're all
// added - audioDeviceAboutToStart() prepares whatever tracks exist at that
// point.
class AudioEngine : private juce::AudioIODeviceCallback,
                     private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    void addTrack (TrackAudioSource* track);
    void start();

    // Swaps tracks[index] for newTrack under the tracks lock, so the audio
    // thread's mix loop never sees a half-replaced vector. The caller owns
    // both tracks and is responsible for calling prepareToPlay on newTrack
    // first and for not destroying the old one until after this returns.
    void replaceTrackAt (int index, TrackAudioSource* newTrack);

    void requestState (TransportState newState) { requestedState.store (newState); }
    TransportState getRequestedState() const { return requestedState.load(); }

    double getElapsedSamples() const { return elapsedSamples.load(); }
    double getCurrentSampleRate() const { return currentSampleRate; }

    void startSimulatingAudioInput (std::vector<float> samples) { audioInputSimulator.start (std::move (samples)); }

    void setTempo (Tempo tempo);
    Tempo getTempo() const;

    void setMetronomeEnabled (bool shouldBeEnabled) { metronome.setEnabled (shouldBeEnabled); }
    bool isMetronomeEnabled() const { return metronome.isEnabled(); }
    void setMetronomeGain (float newGain) { metronome.setGain (newGain); }

private:
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                            float* const* outputChannelData, int numOutputChannels,
                                            int numSamples, const juce::AudioIODeviceCallbackContext&) override;

    void timerCallback() override;

    void mixTrackIntoOutput (TrackAudioSource& track, juce::AudioBuffer<float>& outputBuffer, int numSamples,
                              TransportState globalState, double transportElapsedSamples, double bpm,
                              const juce::AudioBuffer<const float>* inputBuffer, bool anySoloed);

    juce::AudioDeviceManager deviceManager;

    std::vector<TrackAudioSource*> tracks;
    juce::CriticalSection tracksLock;

    AudioInputSimulator audioInputSimulator;
    juce::AudioBuffer<float> simulatedInputScratch;
    juce::AudioBuffer<float> scratchBuffer;

    Metronome metronome;
    std::atomic<double> tempoBpm { 120.0 };
    std::atomic<int> tempoNumerator { 4 };
    std::atomic<int> tempoDenominator { 4 };

    std::atomic<TransportState> requestedState { TransportState::Idle };
    TransportState currentState = TransportState::Idle;
    std::atomic<double> elapsedSamples { 0.0 };
    double currentSampleRate = 44100.0;

    // Audio-callback timing diagnostics, printed once a second by this
    // class's own Timer. previousCallbackStart/havePreviousCallbackStart are
    // only ever touched by the audio thread; the atomics are read/reset from
    // the message thread by timerCallback().
    std::chrono::steady_clock::time_point previousCallbackStart;
    bool havePreviousCallbackStart = false;
    std::atomic<double> maxCallbackGapMs { 0.0 };
    std::atomic<double> maxCallbackDurationMs { 0.0 };
    double expectedCallbackIntervalMs = 0.0;
};
