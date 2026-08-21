#include "ProjectFile.h"
#include "../Tracks/TrackFactory.h"

namespace ProjectFile
{
    juce::File audioFolderFor (const juce::File& projectFile)
    {
        return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + "_Audio");
    }

    void save (const juce::File& file, const std::vector<std::unique_ptr<TrackBase>>& tracks)
    {
        juce::XmlElement root ("CAPT_PROJECT");
        auto audioFolder = audioFolderFor (file);

        for (auto& track : tracks)
            root.addChildElement (track->toXml (audioFolder).release());

        root.writeTo (file);
    }

    void load (const juce::File& file, std::vector<std::unique_ptr<TrackBase>>& tracks,
               const std::function<void (int index, TrackType desiredType)>& ensureTrackType)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return;

        auto audioFolder = audioFolderFor (file);
        int index = 0;

        for (auto* trackXml : xml->getChildIterator())
        {
            if (index >= (int) tracks.size())
                break;

            auto desiredType = trackTypeForXmlTag (trackXml->getTagName());

            if (tracks[(size_t) index]->getType() != desiredType)
                ensureTrackType (index, desiredType);

            tracks[(size_t) index]->fromXml (*trackXml, audioFolder);
            ++index;
        }
    }
}
