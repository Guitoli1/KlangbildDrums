#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "VintageUI.h"
#include "RackStyle.h"

class BandAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BandAudioProcessorEditor (BandAudioProcessor&);
    ~BandAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob { juce::Slider slider; juce::String name; std::unique_ptr<SliderAttachment> attachment; bool big = false; };

    Knob& addKnob (const juce::String& paramID, const juce::String& name, bool big, const juce::String& steps = {});
    void setupSwitch (juce::ToggleButton& b, const juce::String& paramID, const juce::String& onText, const juce::String& offText, juce::Colour lamp);
    void timerCallback() override;
    void renderPanel();

    BandAudioProcessor& proc;
    RackLookAndFeel lnf;

    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton onSwitch, autoSwitch;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    juce::ComboBox presetBox;
    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    // zwei laufende Bandspulen im Kopfbereich
    struct Reels : public juce::Component
    {
        static constexpr float reelRadius = 74.0f, packL = reelRadius * 0.86f, packR = reelRadius * 0.55f;
        float angleL = 0.0f, angleR = 0.0f;
        void paint (juce::Graphics&) override;
        static void drawReel (juce::Graphics&, juce::Point<float> c, float radius, float packRadius, float angle);
    } reels;
    int64_t lastSamples = 0;

    static constexpr int rackW = 1280, rackH = 620;
    juce::Rectangle<int> nameplate, vuBezelL, vuBezelR, tagArea;
    std::array<juce::Rectangle<int>, 4> sections;
    std::array<juce::Rectangle<int>, 2> switchLabels;
    juce::Image panelImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandAudioProcessorEditor)
};
