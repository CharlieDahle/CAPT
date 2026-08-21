#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Tracks/TrackBase.h"
#include <functional>

// Save/load orchestration for a .captproj file. Track (de)serialization
// itself stays on TrackBase::toXml/fromXml - this only owns the XML
// document, the audio-file folder convention, and looping over tracks.
namespace ProjectFile
{
    juce::File audioFolderFor (const juce::File& projectFile);

    void save (const juce::File& file, const std::vector<std::unique_ptr<TrackBase>>& tracks);

    // ensureTrackType is called before fromXml whenever a track's saved
    // type doesn't match its current type, so the caller can rebuild the
    // track's UI component first - that's not this file's job.
    void load (const juce::File& file, std::vector<std::unique_ptr<TrackBase>>& tracks,
               const std::function<void (int index, TrackType desiredType)>& ensureTrackType);
}
