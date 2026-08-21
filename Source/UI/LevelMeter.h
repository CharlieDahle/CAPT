#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>

class LevelMeter : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (const std::atomic<float>& levelToWatch)
        : level (levelToWatch)
    {
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);

        auto levelValue = juce::jlimit (0.0f, 1.0f, level.load());
        auto barWidth = (int) ((float) getWidth() * levelValue);

        g.setColour (juce::Colours::limegreen);
        g.fillRect (0, 0, barWidth, getHeight());
    }

private:
    void timerCallback() override { repaint(); }

    const std::atomic<float>& level;
};
