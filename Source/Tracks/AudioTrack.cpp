#include "AudioTrack.h"
#include "../UI/GridPainter.h"

AudioTrack::AudioTrack (const juce::String& trackName)
    : TrackBase (trackName, TrackType::Audio)
{
    placeholderLabel.setText ("Audio track (waveform view coming soon)", juce::dontSendNotification);
    placeholderLabel.setJustificationType (juce::Justification::centred);
    placeholderLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (placeholderLabel);

    levelMeter.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (levelMeter);
}

void AudioTrack::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                                   TransportState globalState, double elapsedSamples, double /*bpm*/,
                                   const juce::AudioBuffer<const float>* inputBuffer)
{
    if (isArmed() && inputBuffer != nullptr && inputBuffer->getNumChannels() > 0)
    {
        auto* inputData = inputBuffer->getReadPointer (0, startSample);
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (inputData[i]));

        currentLevel.store (juce::jmax (peak, currentLevel.load() * 0.85f));
    }
    else
    {
        currentLevel.store (currentLevel.load() * 0.85f);
    }

    auto effectiveMode = globalState;

    if (globalState == TransportState::Recording && ! isArmed())
        effectiveMode = TransportState::Playing;

    // recordedSamples is also touched from the message thread (Save reads
    // it, Load replaces it outright) with no other synchronization, so
    // every access - here and in getLastEventTimeSamples/toXml/fromXml -
    // has to go through this lock.
    const juce::ScopedLock lock (recordedSamplesLock);

    if (effectiveMode != previousEffectiveMode)
    {
        if (effectiveMode == TransportState::Recording)
            recordedSamples.clear();

        previousEffectiveMode = effectiveMode;
    }

    if (effectiveMode == TransportState::Recording && inputBuffer != nullptr && inputBuffer->getNumChannels() > 0)
    {
        auto* inputData = inputBuffer->getReadPointer (0, startSample);
        recordedSamples.insert (recordedSamples.end(), inputData, inputData + numSamples);
    }
    else if (effectiveMode == TransportState::Playing)
    {
        auto playbackIndex = (size_t) elapsedSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            auto sampleIndex = playbackIndex + (size_t) i;

            if (sampleIndex >= recordedSamples.size())
                break;

            auto sampleValue = recordedSamples[sampleIndex];

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                buffer.addSample (channel, startSample + i, sampleValue);
        }
    }
}

double AudioTrack::getLastEventTimeSamples() const
{
    const juce::ScopedLock lock (recordedSamplesLock);
    return (double) recordedSamples.size();
}

std::unique_ptr<juce::XmlElement> AudioTrack::toXml (const juce::File& audioFolder) const
{
    auto trackXml = std::make_unique<juce::XmlElement> ("AUDIO_TRACK");
    writeVolumeAttribute (*trackXml);
    writeMuteSoloAttributes (*trackXml);

    const juce::ScopedLock lock (recordedSamplesLock);

    if (! recordedSamples.empty())
    {
        auto fileName = getTrackName() + ".wav";
        audioFolder.createDirectory();
        writeWavFile (audioFolder.getChildFile (fileName));
        trackXml->setAttribute ("audioFile", fileName);
    }

    return trackXml;
}

void AudioTrack::fromXml (const juce::XmlElement& trackXml, const juce::File& audioFolder)
{
    setVolumeFromXml (trackXml.getDoubleAttribute ("volume", 0.8));
    setMuted (trackXml.getBoolAttribute ("muted", false));
    setSoloed (trackXml.getBoolAttribute ("soloed", false));

    auto fileName = trackXml.getStringAttribute ("audioFile");

    const juce::ScopedLock lock (recordedSamplesLock);

    if (fileName.isNotEmpty())
        readWavFile (audioFolder.getChildFile (fileName));
    else
        recordedSamples.clear();
}

void AudioTrack::paint (juce::Graphics& g)
{
    TrackBase::paint (g);

    if (gridSettingsProvider != nullptr)
    {
        juce::Rectangle<float> contentArea (0.0f, kTrackHeaderHeight, (float) getWidth(), (float) getHeight() - kTrackHeaderHeight);
        GridPainter::paintVerticalGridLines (g, contentArea, kKeyboardStripWidth, gridSettingsProvider(),
                                              juce::Colours::black.withAlpha (0.3f), juce::Colours::black.withAlpha (0.08f));
    }

    g.setColour (juce::Colours::darkslategrey);
    g.fillRect (0, (int) kTrackHeaderHeight, (int) kKeyboardStripWidth, getHeight() - (int) kTrackHeaderHeight);
}

void AudioTrack::resizedContent (juce::Rectangle<int> contentArea)
{
    auto meterArea = contentArea.removeFromTop (24).reduced (2);
    meterArea.removeFromLeft ((int) kKeyboardStripWidth);
    levelMeter.setBounds (meterArea);

    placeholderLabel.setBounds (contentArea);
}

void AudioTrack::writeWavFile (const juce::File& file) const
{
    file.deleteFile();

    std::unique_ptr<juce::OutputStream> outputStream (new juce::FileOutputStream (file));

    auto options = juce::AudioFormatWriterOptions()
                       .withSampleRate (currentSampleRate)
                       .withNumChannels (1)
                       .withBitsPerSample (16);

    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor (outputStream, options);

    if (writer == nullptr)
        return;

    auto* channelPtr = recordedSamples.data();
    writer->writeFromFloatArrays (&channelPtr, 1, (int) recordedSamples.size());
}

void AudioTrack::readWavFile (const juce::File& file)
{
    recordedSamples.clear();

    if (! file.existsAsFile())
        return;

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader (
        wavFormat.createReaderFor (new juce::FileInputStream (file), true));

    if (reader == nullptr)
        return;

    recordedSamples.resize ((size_t) reader->lengthInSamples);
    auto* destPtr = recordedSamples.data();
    reader->read (&destPtr, 1, 0, (int) reader->lengthInSamples);
}
