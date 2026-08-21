#pragma once

#include "TrackBase.h"
#include "../UI/LevelMeter.h"

// Records mono microphone input into an in-memory buffer and plays it back;
// persisted to a .wav alongside the project file. No waveform view yet.
class AudioTrack : public TrackBase
{
public:
    explicit AudioTrack (const juce::String& trackName);

    void prepareToPlay (double sampleRate) override { currentSampleRate = sampleRate; }

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           TransportState globalState, double elapsedSamples,
                           const juce::AudioBuffer<const float>* inputBuffer) override;

    double getLastEventTimeSamples() const override;

    std::unique_ptr<juce::XmlElement> toXml (const juce::File& audioFolder) const override;
    void fromXml (const juce::XmlElement& trackXml, const juce::File& audioFolder) override;

    void paint (juce::Graphics& g) override;

protected:
    void resizedContent (juce::Rectangle<int> contentArea) override;

private:
    // Both of these assume recordedSamplesLock is already held - they're
    // only ever called from toXml/fromXml, which hold it for their whole body.
    void writeWavFile (const juce::File& file) const;
    void readWavFile (const juce::File& file);

    juce::Label placeholderLabel;
    std::atomic<float> currentLevel { 0.0f };
    LevelMeter levelMeter { currentLevel };

    // recordedSamples is written on the audio thread while recording and
    // read from the message thread on Save/Load - every access to it goes
    // through this lock.
    juce::CriticalSection recordedSamplesLock;
    std::vector<float> recordedSamples;
    TransportState previousEffectiveMode = TransportState::Idle;
    double currentSampleRate = 44100.0;
};
