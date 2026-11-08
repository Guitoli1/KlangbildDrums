#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PlateDsp.h"
#include "EQDsp.h"

class PlatteAudioProcessor : public juce::AudioProcessor
{
public:
    PlatteAudioProcessor();
    ~PlatteAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Preset>& presets();
    int getNumPrograms() override { return (int) presets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vuL { 0 }, vuR { 0 }, meterL { 0 }, meterR { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    plate::Reverb reverb;
    eq::Coeffs lowCut;
    eq::BiquadState lcL, lcR;
    float lastLowCut = -1;

    std::atomic<float>* pDecay = nullptr; std::atomic<float>* pPre = nullptr; std::atomic<float>* pDamp = nullptr;
    std::atomic<float>* pLow = nullptr;   std::atomic<float>* pWidth = nullptr; std::atomic<float>* pMod = nullptr;
    std::atomic<float>* pMix = nullptr; std::atomic<float>* pOn = nullptr;

    juce::SmoothedValue<float> smMix, smWidth, smWetGain;
    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    int currentProgram = 0;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlatteAudioProcessor)
};
