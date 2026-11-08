#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr dbAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout BusAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "threshold", 1 }, "Threshold", juce::NormalisableRange<float> (-40.0f, 0.0f, 0.1f), -12.0f, dbAttr()));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ratio", 1 }, "Ratio", juce::StringArray { "2:1", "4:1", "10:1" }, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "attack", 1 }, "Attack",
        juce::StringArray { "0,1 ms", "0,3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 4));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "release", 1 }, "Release",
        juce::StringArray { "0,1 s", "0,3 s", "0,6 s", "1,2 s", "Auto" }, 4));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scfilter", 1 }, "Sidechain-Filter",
        juce::StringArray { "Aus", "60 Hz", "90 Hz", "150 Hz" }, 0));
    l.add (std::make_unique<APF> (juce::ParameterID { "makeup", 1 }, "Makeup", juce::NormalisableRange<float> (0.0f, 20.0f, 0.01f), 0.0f, dbAttr()));
    l.add (std::make_unique<APF> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; })
              .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue(); })));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "Kompressor an", true));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "meter", 1 }, "Anzeige", juce::StringArray { "Pegelreduktion", "Ausgang" }, 0));
    return l;
}

const std::vector<BusAudioProcessor::Preset>& BusAudioProcessor::presets()
{
    // Indizes: ratio 0=2:1 1=4:1 2=10:1 · attack 0=0,1 … 5=30 ms · release 0=0,1 … 3=1,2 s, 4=Auto · scfilter 0=aus 1=60 2=90 3=150 Hz
    static const std::vector<Preset> p = {
        { "Standard",          { { "threshold", -12 }, { "ratio", 1 }, { "attack", 4 }, { "release", 4 }, { "scfilter", 0 }, { "makeup", 0 }, { "mix", 100 } } },
        { "Summe: Kleber",     { { "threshold", -14 }, { "ratio", 0 }, { "attack", 5 }, { "release", 4 }, { "scfilter", 2 }, { "makeup", 2 }, { "mix", 100 } } },
        { "Drumbus: Punch",    { { "threshold", -20 }, { "ratio", 1 }, { "attack", 4 }, { "release", 0 }, { "scfilter", 1 }, { "makeup", 4 }, { "mix", 75 } } },
        { "Drumbus: Parallel", { { "threshold", -30 }, { "ratio", 2 }, { "attack", 0 }, { "release", 0 }, { "scfilter", 0 }, { "makeup", 10 }, { "mix", 35 } } },
        { "Gitarren-Bus",      { { "threshold", -18 }, { "ratio", 1 }, { "attack", 3 }, { "release", 1 }, { "scfilter", 3 }, { "makeup", 3 }, { "mix", 100 } } },
        { "Gesang-Bus",        { { "threshold", -16 }, { "ratio", 0 }, { "attack", 3 }, { "release", 4 }, { "scfilter", 3 }, { "makeup", 2 }, { "mix", 100 } } },
        { "Bläser / Keys",     { { "threshold", -15 }, { "ratio", 0 }, { "attack", 4 }, { "release", 2 }, { "scfilter", 2 }, { "makeup", 2 }, { "mix", 100 } } },
    };
    return p;
}

void BusAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

const juce::String BusAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size())
               ? juce::String (juce::CharPointer_UTF8 (presets()[(size_t) index].name)) : juce::String();
}

BusAudioProcessor::BusAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildBus", createLayout())
{
    pThr = apvts.getRawParameterValue ("threshold"); pRatio = apvts.getRawParameterValue ("ratio");
    pAtt = apvts.getRawParameterValue ("attack");    pRel = apvts.getRawParameterValue ("release");
    pFilter = apvts.getRawParameterValue ("scfilter"); pMakeup = apvts.getRawParameterValue ("makeup");
    pMix = apvts.getRawParameterValue ("mix");       pOn = apvts.getRawParameterValue ("on");
}

bool BusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && in == out;
}

void BusAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    comp.prepare (sr);
    smMakeup.reset (sr, 0.03); smMakeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pMakeup->load()));
    smMix.reset (sr, 0.03);    smMix.setCurrentAndTargetValue (pMix->load() * 0.01f);
    smOn.reset (sr, 0.02);     smOn.setCurrentAndTargetValue (pOn->load());
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuState = 0;
}

void BusAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    if (numCh == 0)
        return;

    comp.setParams (pThr->load(), (int) pRatio->load(), (int) pAtt->load(), (int) pRel->load(), (int) pFilter->load());
    smMakeup.setTargetValue (juce::Decibels::decibelsToGain (pMakeup->load()));
    smMix.setTargetValue (pMix->load() * 0.01f);
    smOn.setTargetValue (pOn->load() > 0.5f ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    float maxGr = 0, pk = 0;

    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i], dr = R ? R[i] : dl;
        const float gr = (float) comp.process (dl, dr);
        maxGr = juce::jmax (maxGr, gr);

        // Mix = Parallelkompression; "aus" blendet weich auf das unbearbeitete Signal
        const float wetGain = juce::Decibels::decibelsToGain (-gr) * smMakeup.getNextValue();
        const float mix = smMix.getNextValue() * smOn.getNextValue();
        const float g = 1.0f + mix * (wetGain - 1.0f);
        L[i] = dl * g;
        if (R) R[i] = dr * g;

        const float o = juce::jmax (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f);
        vuState += (o - vuState) * vuCoef;
        pk = juce::jmax (pk, o);
    }

    grDb.store (pOn->load() > 0.5f ? maxGr : 0.0f);
    vuOut.store (vuState);
    peakOut.store (juce::jmax (pk, peakOut.load()));
}

void BusAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
    apvts.replaceState (tree);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
}

juce::AudioProcessorEditor* BusAudioProcessor::createEditor() { return new BusAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BusAudioProcessor(); }
#endif
