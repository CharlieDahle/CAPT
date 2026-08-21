#pragma once

#include "TrackBase.h"
#include "../UI/PianoRoll.h"
#include <array>
#include <map>

struct SineWaveSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override         { return true; }
    bool appliesToChannel (int) override       { return true; }
};

struct SineWaveVoice : public juce::SynthesiserVoice
{
    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        phase = 0.0;
        auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        phaseDelta = frequency / getSampleRate();
        level = velocity * 0.2f;
        active = true;
    }

    void stopNote (float /*velocity*/, bool /*allowTailOff*/) override
    {
        active = false;
        clearCurrentNote();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                           int startSample, int numSamples) override
    {
        if (! active)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sampleValue = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase) * level;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, startSample + i, sampleValue);

            phase += phaseDelta;
            if (phase >= 1.0)
                phase -= 1.0;
        }
    }

private:
    bool active = false;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float level = 0.0f;
};

class MidiTrack : public TrackBase
{
public:
    explicit MidiTrack (const juce::String& trackName);

    void prepareToPlay (double sampleRate) override;

    // Pairs the raw on/off event log into editor-friendly notes. Safe from
    // the message thread since the audio thread never touches
    // recordedEvents/numRecordedEvents while idle, the only time this is read.
    std::vector<PianoRollView::Note> getRecordedNotesSnapshot() const;

    // Only called while idle - see the PianoRollCanvas class comment for
    // why that makes this safe without locking.
    void setNotesFromEditor (const std::vector<PianoRollView::Note>& notes);

    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           TransportState globalState, double elapsedSamples,
                           const juce::AudioBuffer<const float>*) override;

    void injectTestNote (int noteNumber, float velocity, bool isNoteOn) override;

    double getLastEventTimeSamples() const override;

    std::unique_ptr<juce::XmlElement> toXml (const juce::File&) const override;
    void fromXml (const juce::XmlElement& trackXml, const juce::File&) override;

protected:
    void resizedContent (juce::Rectangle<int> contentArea) override { pianoRollView.setBounds (contentArea); }

    void selectionChanged (bool isSelected) override { pianoRollView.setSelected (isSelected); }
    void expandedChanged (bool isExpandedNow) override { pianoRollView.setExpanded (isExpandedNow); }

private:
    void closeOutHeldRecordingNotes (double stopTimeSamples);

    juce::MidiKeyboardState keyboardState;
    PianoRollView pianoRollView;
    juce::Synthesiser synth;
    double currentSampleRate = 44100.0;
    std::atomic<TransportState> lastGlobalState { TransportState::Idle };

    int nextPlaybackIndex = 0;
    TransportState previousEffectiveMode = TransportState::Idle;

    static constexpr int maxRecordedEvents = 4096;
    std::array<RecordedNoteEvent, maxRecordedEvents> recordedEvents;
    std::atomic<int> numRecordedEvents { 0 };
    std::vector<int> heldRecordingNotes;
};
