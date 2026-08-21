#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Core/Types.h"
#include "../Core/Tempo.h"
#include "../UI/IconToggleButton.h"

class TrackBase : public juce::Component
{
public:
    TrackBase (const juce::String& trackName, TrackType type);
    ~TrackBase() override = default;

    virtual void prepareToPlay (double sampleRate) = 0;

    // inputBuffer is this block's raw mic input, nullptr if unavailable;
    // only AudioTrack uses it. bpm is the current tempo, read fresh every
    // block - only MidiTrack uses it (to convert between real time and beat
    // positions); AudioTrack ignores it since recorded audio isn't
    // tempo-relative.
    virtual void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                   TransportState globalState, double elapsedSamples, double bpm,
                                   const juce::AudioBuffer<const float>* inputBuffer) = 0;

    virtual double getLastEventTimeSamples() const = 0;

    virtual std::unique_ptr<juce::XmlElement> toXml (const juce::File& audioFolder) const = 0;
    virtual void fromXml (const juce::XmlElement& trackXml, const juce::File& audioFolder) = 0;

    virtual void injectTestNote (int /*noteNumber*/, float /*velocity*/, bool /*isNoteOn*/) {}

    // No-op for tracks with nothing grid-aware (AudioTrack) - only MidiTrack
    // overrides these.
    virtual void setGridSettingsProvider (std::function<GridSettings()> /*provider*/) {}
    virtual void setTempoProvider (std::function<Tempo()> /*provider*/) {}
    virtual void quantize (int /*stepsPerBeat*/) {}

    float getVolume() const { return volume.load(); }
    void setVolume (float newVolume) { volume.store (newVolume); }
    bool isArmed() const { return armed.load(); }
    void setArmed (bool newArmed) { armed.store (newArmed); }
    bool isMuted() const { return muted.load(); }
    bool isSoloed() const { return soloed.load(); }
    TrackType getType() const { return trackType; }
    juce::String getTrackName() const { return nameLabel.getText(); }

    void setSelected (bool newSelected);
    bool isSelected() const { return selected; }

    // Only one track is expanded at a time - the owner calls this on every
    // track when it changes. The mute/solo/select buttons live in the same
    // gutter the piano keys use when expanded, so they only show up
    // collapsed.
    void setExpanded (bool newExpanded);
    bool isExpanded() const { return expanded; }

    std::function<void()> onTypeToggleRequested;
    std::function<void()> onSelected;
    std::function<void()> onExpandToggleRequested;

    void paint (juce::Graphics& g) override;

    // A plain click always just selects, never expands, so it never
    // surprises you with a layout change.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void resized() final;

protected:
    virtual void resizedContent (juce::Rectangle<int> contentArea) = 0;

    virtual void selectionChanged (bool /*isSelected*/) {}
    virtual void expandedChanged (bool /*isExpanded*/) {}

    // For content that handles its own clicks: first click selects, a
    // click while already selected toggles expansion instead.
    void selectOrToggleExpand();

    void setVolumeFromXml (double newVolume) { volume.store ((float) newVolume); }
    void writeVolumeAttribute (juce::XmlElement& trackXml) const { trackXml.setAttribute ("volume", (double) volume.load()); }

private:
    TrackType trackType;

    juce::Label nameLabel;
    IconToggleButton muteButton { "M", juce::Colours::orangered };
    IconToggleButton soloButton { "S", juce::Colours::gold };
    IconToggleButton selectButton { {}, juce::Colours::white };

    std::atomic<float> volume { 0.8f };
    std::atomic<bool> armed { false };
    std::atomic<bool> muted { false };
    std::atomic<bool> soloed { false };
    bool selected = false;
    bool expanded = false;
};
