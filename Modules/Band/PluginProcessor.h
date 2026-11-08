#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "TapeDsp.h"

class BandAudioProcessor : public juce::AudioProcessor
{
public:
    BandAudioProcessor();
    ~BandAudioProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

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
    std::atomic<float> vuL { 0 }, vuR { 0 }, peakL { 0 }, peakR { 0 };
    std::atomic<int64_t> samplesRun { 0 };   // für die Spulen-Animation

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    tape::Machine machine;   // läuft mit doppelter Samplerate
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> work;
    int maxBlock = 512;
    std::array<std::vector<float>, 2> dryDelay;
    int dryPos = 0, latency = 0;

    std::atomic<float>* pIn = nullptr; std::atomic<float>* pOut = nullptr; std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pBias = nullptr; std::atomic<float>* pWow = nullptr; std::atomic<float>* pFlutter = nullptr;
    std::atomic<float>* pHiss = nullptr; std::atomic<float>* pOn = nullptr; std::atomic<float>* pAuto = nullptr;

    juce::SmoothedValue<float> smIn, smOut, smOn;
    float vuStateL = 0, vuStateR = 0, vuCoef = 0.001f;
    int currentProgram = 0;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandAudioProcessor)
};
