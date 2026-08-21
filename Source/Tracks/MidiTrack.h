#pragma once

#include "TrackBase.h"
#include "../UI/PianoRoll.h"
#include <array>
#include <map>

struct SynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override         { return true; }
    bool appliesToChannel (int) override       { return true; }
};

// Naive (non-band-limited) oscillator, on purpose - aliasing on saw/square/
// triangle at high notes is a real thing you'd fix with band-limiting, but
// this is meant to be a legible starting point for learning synthesis, not
// production-quality DSP.
inline float generateOscillatorSample (OscillatorWaveform waveform, double phase)
{
    switch (waveform)
    {
        case OscillatorWaveform::Saw:      return (float) (2.0 * phase - 1.0);
        case OscillatorWaveform::Square:   return phase < 0.5 ? 1.0f : -1.0f;
        case OscillatorWaveform::Triangle: return (float) (4.0 * std::abs (phase - 0.5) - 1.0);
        case OscillatorWaveform::Sine:
        default:                          return (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
    }
}

struct SynthVoice : public juce::SynthesiserVoice
{
    // waveformToUse is owned by the MidiTrack that creates this voice (and
    // outlives it) - read fresh every sample rather than captured once, so
    // switching waveforms takes effect immediately, even mid-note.
    explicit SynthVoice (const std::atomic<OscillatorWaveform>& waveformToUse)
        : waveform (waveformToUse)
    {
    }

    bool canPlaySound (juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<SynthSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                     juce::SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        phase = 0.0;
        auto frequency = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        phaseDelta = frequency / getSampleRate();
        level = velocity * 0.2f;
        envelope = 0.0f;
        releasing = false;
        // 5ms fade - without it, note-on/off is a hard jump in the middle of
        // the waveform (not a zero crossing), heard as a click; rapid
        // retriggering turns that into audible crackle.
        envelopeStep = 1.0f / (float) juce::jmax (1.0, 0.005 * getSampleRate());
        active = true;
    }

    void stopNote (float /*velocity*/, bool /*allowTailOff*/) override
    {
        // Always fade out, even on a forced stop (voice stealing / allNotesOff)
        // - clearCurrentNote() is deferred to renderNextBlock once the fade
        // finishes, so the voice still reports itself busy for its tail.
        releasing = true;
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                           int startSample, int numSamples) override
    {
        if (! active)
            return;

        auto currentWaveform = waveform.load();

        for (int i = 0; i < numSamples; ++i)
        {
            envelope = releasing ? juce::jmax (0.0f, envelope - envelopeStep)
                                  : juce::jmin (1.0f, envelope + envelopeStep);

            auto sampleValue = generateOscillatorSample (currentWaveform, phase) * level * envelope;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, startSample + i, sampleValue);

            phase += phaseDelta;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        if (releasing && envelope <= 0.0f)
        {
            active = false;
            clearCurrentNote();
        }
    }

private:
    const std::atomic<OscillatorWaveform>& waveform;
    bool active = false;
    bool releasing = false;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float level = 0.0f;
    float envelope = 0.0f;
    float envelopeStep = 1.0f;
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
                           TransportState globalState, double elapsedSamples, double bpm,
                           const juce::AudioBuffer<const float>*) override;

    void injectTestNote (int noteNumber, float velocity, bool isNoteOn) override;

    void setGridSettingsProvider (std::function<GridSettings()> provider) override
    {
        pianoRollView.setGridSettingsProvider (std::move (provider));
    }

    // Retained (not just forwarded) - needed on the message thread by
    // getLastEventTimeSamples() to convert the last recorded beat position
    // back to samples using the current tempo.
    void setTempoProvider (std::function<Tempo()> provider) override { tempoProvider = std::move (provider); }

    void quantize (int stepsPerBeat) override;

    void setWaveform (OscillatorWaveform newWaveform) override { waveform.store (newWaveform); }
    OscillatorWaveform getWaveform() const override { return waveform.load(); }

    double getLastEventTimeSamples() const override;

    std::unique_ptr<juce::XmlElement> toXml (const juce::File&) const override;
    void fromXml (const juce::XmlElement& trackXml, const juce::File&) override;

protected:
    void resizedContent (juce::Rectangle<int> contentArea) override { pianoRollView.setBounds (contentArea); }

    void selectionChanged (bool isSelected) override { pianoRollView.setSelected (isSelected); }
    void expandedChanged (bool isExpandedNow) override { pianoRollView.setExpanded (isExpandedNow); }

private:
    void closeOutHeldRecordingNotes (double stopTimeSamples, double bpm);

    juce::MidiKeyboardState keyboardState;
    PianoRollView pianoRollView;
    juce::Synthesiser synth;
    std::atomic<OscillatorWaveform> waveform { OscillatorWaveform::Sine };
    double currentSampleRate = 44100.0;
    std::function<Tempo()> tempoProvider;
    std::atomic<TransportState> lastGlobalState { TransportState::Idle };

    int nextPlaybackIndex = 0;
    TransportState previousEffectiveMode = TransportState::Idle;

    static constexpr int maxRecordedEvents = 4096;
    std::array<RecordedNoteEvent, maxRecordedEvents> recordedEvents;
    std::atomic<int> numRecordedEvents { 0 };
    std::vector<int> heldRecordingNotes;
};
