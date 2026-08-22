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

// Live-tweakable ADSR parameters shared by every voice in the synth - same
// pattern as `waveform` below: held here (not per-voice) so a slider change
// is picked up by whichever voice reads it next, with each SynthVoice
// keeping only a const reference to it rather than its own copy.
struct EnvelopeState
{
    std::atomic<float> attackSeconds { 0.01f };
    std::atomic<float> decaySeconds { 0.1f };
    std::atomic<float> sustainLevel { 0.8f };
    std::atomic<float> releaseSeconds { 0.2f };
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

enum class EnvelopeStage { Attack, Decay, Sustain, Release, Idle };

struct SynthVoice : public juce::SynthesiserVoice
{
    // waveformToUse/envelopeToUse are owned by the MidiTrack that creates
    // this voice (and outlive it) - read fresh at note-start/note-stop
    // (waveform: every sample) rather than captured once at construction,
    // so tweaking a slider takes effect on the next note without having to
    // rebuild the voice.
    SynthVoice (const std::atomic<OscillatorWaveform>& waveformToUse, const EnvelopeState& envelopeToUse)
        : waveform (waveformToUse), envelopeParams (envelopeToUse)
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

        // Captured once per note-on (like frequency/level above), not read
        // fresh every sample - a mid-note slider tweak takes effect on the
        // *next* note struck, keeping the per-sample loop below simple.
        auto attackSeconds = envelopeParams.attackSeconds.load();
        auto decaySeconds = envelopeParams.decaySeconds.load();
        noteSustainLevel = envelopeParams.sustainLevel.load();

        envelopeLevel = 0.0f;
        stage = EnvelopeStage::Attack;
        // Step size that reaches the stage's target in exactly attack/decay
        // seconds; jmax guards a 0-second stage (an instant jump) from
        // dividing by zero.
        attackStep = 1.0f / juce::jmax (1.0f, attackSeconds * (float) getSampleRate());
        decayStep = (1.0f - noteSustainLevel) / juce::jmax (1.0f, decaySeconds * (float) getSampleRate());
    }

    void stopNote (float /*velocity*/, bool /*allowTailOff*/) override
    {
        // Always release, even on a forced stop (voice stealing / allNotesOff)
        // - clearCurrentNote() is deferred to renderNextBlock until the
        // release stage reaches 0, so the voice still reports itself busy
        // for its tail.
        auto releaseSeconds = envelopeParams.releaseSeconds.load();
        // Scaled by the level being released *from*, not a fixed assumption
        // that it's at sustain - a note let go mid-attack/decay still takes
        // the full release time to reach silence, just starting from
        // wherever the envelope currently is.
        releaseStep = envelopeLevel / juce::jmax (1.0f, releaseSeconds * (float) getSampleRate());
        stage = EnvelopeStage::Release;
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                           int startSample, int numSamples) override
    {
        if (stage == EnvelopeStage::Idle)
            return;

        auto currentWaveform = waveform.load();

        for (int i = 0; i < numSamples; ++i)
        {
            switch (stage)
            {
                case EnvelopeStage::Attack:
                    envelopeLevel += attackStep;
                    if (envelopeLevel >= 1.0f) { envelopeLevel = 1.0f; stage = EnvelopeStage::Decay; }
                    break;
                case EnvelopeStage::Decay:
                    envelopeLevel -= decayStep;
                    if (envelopeLevel <= noteSustainLevel) { envelopeLevel = noteSustainLevel; stage = EnvelopeStage::Sustain; }
                    break;
                case EnvelopeStage::Release:
                    envelopeLevel = juce::jmax (0.0f, envelopeLevel - releaseStep);
                    if (envelopeLevel <= 0.0f) { envelopeLevel = 0.0f; stage = EnvelopeStage::Idle; }
                    break;
                case EnvelopeStage::Sustain:
                case EnvelopeStage::Idle:
                default:
                    break;
            }

            auto sampleValue = generateOscillatorSample (currentWaveform, phase) * level * envelopeLevel;

            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
                outputBuffer.addSample (channel, startSample + i, sampleValue);

            phase += phaseDelta;
            if (phase >= 1.0)
                phase -= 1.0;
        }

        if (stage == EnvelopeStage::Idle)
            clearCurrentNote();
    }

private:
    const std::atomic<OscillatorWaveform>& waveform;
    const EnvelopeState& envelopeParams;
    double phase = 0.0;
    double phaseDelta = 0.0;
    float level = 0.0f;

    EnvelopeStage stage = EnvelopeStage::Idle;
    float envelopeLevel = 0.0f;
    float attackStep = 0.0f;
    float decayStep = 0.0f;
    float noteSustainLevel = 0.8f;
    float releaseStep = 0.0f;
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

    void setPlayheadBeatsProvider (std::function<double()> provider) override
    {
        pianoRollView.setPlayheadBeatsProvider (std::move (provider));
    }

    void refreshEditorContents() override { pianoRollView.refreshNotes(); }

    // Retained (not just forwarded) - needed on the message thread by
    // getLastEventTimeSamples() to convert the last recorded beat position
    // back to samples using the current tempo.
    void setTempoProvider (std::function<Tempo()> provider) override { tempoProvider = std::move (provider); }

    void quantize (int stepsPerBeat) override;

    void setWaveform (OscillatorWaveform newWaveform) override { waveform.store (newWaveform); }
    OscillatorWaveform getWaveform() const override { return waveform.load(); }

    void setEnvelope (EnvelopeParams newParams) override;
    EnvelopeParams getEnvelope() const override;

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
    EnvelopeState envelopeState;
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
