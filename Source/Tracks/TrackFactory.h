#pragma once

#include "TrackBase.h"

std::unique_ptr<TrackBase> makeTrack (TrackType type, const juce::String& trackName);
TrackType trackTypeForXmlTag (const juce::String& tagName);
