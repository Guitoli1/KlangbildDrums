#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumsProcessor.h"
#include "VintageUI.h"
#include "PresetBar.h"

class DrumsAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DrumsAudioProcessorEditor (DrumsAudioProcessor&);
    ~DrumsAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Tile
    {
        juce::TextButton key;
        juce::ToggleButton power;
        std::unique_ptr<ButtonAttachment> attachment;
    };

    void selectModule (int index);
    void layoutModuleEditor();
    void timerCallback() override;
    void renderTolex();

    DrumsAudioProcessor& proc;
    VintageLookAndFeel lnf;

    std::array<Tile, DrumsAudioProcessor::numModules> tiles;
    juce::ComboBox presetBox;
    void layoutTiles();

    juce::Slider outSlider;
    std::unique_ptr<SliderAttachment> outAttachment;
    VuMeter vuLeft { lnf, "L" }, vuRight { lnf, "R" };

    juce::Component moduleHolder;
    kb::PresetBar modulePresets;
    std::unique_ptr<juce::AudioProcessorEditor> moduleEditor;
    int selected = -1;

    juce::Rectangle<int> header, moduleArea;
    juce::Image tolexImage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsAudioProcessorEditor)
};
