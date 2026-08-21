#include "TrackFactory.h"
#include "MidiTrack.h"
#include "AudioTrack.h"

std::unique_ptr<TrackBase> makeTrack (TrackType type, const juce::String& trackName)
{
    if (type == TrackType::Audio)
        return std::make_unique<AudioTrack> (trackName);

    return std::make_unique<MidiTrack> (trackName);
}

TrackType trackTypeForXmlTag (const juce::String& tagName)
{
    return tagName == "AUDIO_TRACK" ? TrackType::Audio : TrackType::Midi;
}
