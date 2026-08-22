#include "ProjectFile.h"
#include "../Tracks/TrackFactory.h"

namespace ProjectFile
{
    juce::File audioFolderFor (const juce::File& projectFile)
    {
        return projectFile.getParentDirectory().getChildFile (projectFile.getFileNameWithoutExtension() + "_Audio");
    }

    void save (const juce::File& file, const std::vector<std::unique_ptr<TrackBase>>& tracks, const SessionState& session)
    {
        juce::XmlElement root ("CAPT_PROJECT");
        root.setAttribute ("bpm", session.tempo.bpm);
        root.setAttribute ("timeSigNumerator", session.tempo.timeSignature.numerator);
        root.setAttribute ("timeSigDenominator", session.tempo.timeSignature.denominator);
        root.setAttribute ("metronomeEnabled", session.metronomeEnabled);
        root.setAttribute ("metronomeGain", (double) session.metronomeGain);

        auto audioFolder = audioFolderFor (file);

        for (auto& track : tracks)
            root.addChildElement (track->toXml (audioFolder).release());

        root.writeTo (file);
    }

    void load (const juce::File& file, std::vector<std::unique_ptr<TrackBase>>& tracks,
               const std::function<void (int index, TrackType desiredType)>& ensureTrackType,
               SessionState& session)
    {
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return;

        session.tempo.bpm = xml->getDoubleAttribute ("bpm", 120.0);
        session.tempo.timeSignature.numerator = xml->getIntAttribute ("timeSigNumerator", 4);
        session.tempo.timeSignature.denominator = xml->getIntAttribute ("timeSigDenominator", 4);
        session.metronomeEnabled = xml->getBoolAttribute ("metronomeEnabled", false);
        session.metronomeGain = (float) xml->getDoubleAttribute ("metronomeGain", 0.3);

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
