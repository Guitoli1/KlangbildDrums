#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float n) { return a * std::pow (b / a, n); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
}

Attr unit (const char* u, int decimals)
{
    return Attr().withStringFromValueFunction ([=] (float v, int) { return juce::String (v, decimals).replaceCharacter ('.', ',') + " " + u; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.replaceCharacter (',', '.').getFloatValue(); });
}

constexpr double maxPreDelay = 0.25;
}

juce::AudioProcessorValueTreeState::ParameterLayout PlatteAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "decay", 1 }, "Nachhallzeit", logRange (0.3f, 8.0f), 2.2f, unit ("s", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "predelay", 1 }, "Pre-Delay", juce::NormalisableRange<float> (0.0f, 250.0f, 0.1f), 20.0f, unit ("ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "damping", 1 }, juce::String (juce::CharPointer_UTF8 ("D\xc3\xa4mpfung")), juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f, unit ("%", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "lowcut", 1 }, "Low Cut", logRange (20.0f, 800.0f), 120.0f, unit ("Hz", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "width", 1 }, "Breite", juce::NormalisableRange<float> (0.0f, 150.0f, 0.1f), 100.0f, unit ("%", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "modulation", 1 }, "Modulation", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, unit ("%", 0)));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "Hall an", true));
    l.add (std::make_unique<APF> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, unit ("%", 0)));
    return l;
}

const std::vector<PlatteAudioProcessor::Preset>& PlatteAudioProcessor::presets()
{
    static const std::vector<Preset> p = {
        { "Standard",               { { "decay", 2.2f }, { "predelay", 20 }, { "damping", 35 }, { "lowcut", 120 }, { "width", 100 }, { "modulation", 30 }, { "mix", 25 } } },
        { "Gesang hell",            { { "decay", 2.4f }, { "predelay", 60 }, { "damping", 20 }, { "lowcut", 180 }, { "width", 110 }, { "modulation", 35 }, { "mix", 22 } } },
        { "Snare-Platte",           { { "decay", 1.4f }, { "predelay", 5 },  { "damping", 30 }, { "lowcut", 220 }, { "width", 100 }, { "modulation", 20 }, { "mix", 30 } } },
        { "Kurze Platte",           { { "decay", 0.8f }, { "predelay", 10 }, { "damping", 40 }, { "lowcut", 150 }, { "width", 90 },  { "modulation", 25 }, { "mix", 20 } } },
        { "Lange Ballade",          { { "decay", 4.5f }, { "predelay", 80 }, { "damping", 45 }, { "lowcut", 150 }, { "width", 120 }, { "modulation", 45 }, { "mix", 25 } } },
        { "Gitarre warm",           { { "decay", 1.8f }, { "predelay", 30 }, { "damping", 60 }, { "lowcut", 200 }, { "width", 100 }, { "modulation", 30 }, { "mix", 20 } } },
        { juce::CharPointer_UTF8 ("Send/Aux (100 % Hall)"), { { "decay", 2.2f }, { "predelay", 30 }, { "damping", 35 }, { "lowcut", 150 }, { "width", 100 }, { "modulation", 30 }, { "mix", 100 } } },
    };
    return p;
}

void PlatteAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

const juce::String PlatteAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size())
               ? juce::String (juce::CharPointer_UTF8 (presets()[(size_t) index].name)) : juce::String();
}

PlatteAudioProcessor::PlatteAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildPlatte", createLayout())
{
    pDecay = apvts.getRawParameterValue ("decay");    pPre = apvts.getRawParameterValue ("predelay");
    pDamp = apvts.getRawParameterValue ("damping");   pLow = apvts.getRawParameterValue ("lowcut");
    pWidth = apvts.getRawParameterValue ("width");    pMod = apvts.getRawParameterValue ("modulation");
    pMix = apvts.getRawParameterValue ("mix");      pOn = apvts.getRawParameterValue ("on");
}

bool PlatteAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void PlatteAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    reverb.prepare (sr, maxPreDelay);
    lastLowCut = -1;
    lcL.reset(); lcR.reset();
    smMix.reset (sr, 0.03);     smMix.setCurrentAndTargetValue (pMix->load() * 0.01f * (pOn->load() > 0.5f ? 1.0f : 0.0f));
    smWidth.reset (sr, 0.03);   smWidth.setCurrentAndTargetValue (pWidth->load() * 0.01f);
    smWetGain.reset (sr, 0.1);  smWetGain.setCurrentAndTargetValue (std::pow (2.0f / pDecay->load(), 0.15f));
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void PlatteAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inCh = getTotalNumInputChannels();
    const int n = buffer.getNumSamples();
    if (inCh == 0 || buffer.getNumChannels() < 2)
        return;

    reverb.setParams (pDecay->load(), pDamp->load() * 0.01f, pPre->load(), pMod->load() * 0.01f);
    if (! juce::exactlyEqual (pLow->load(), lastLowCut))
    {
        lastLowCut = pLow->load();
        lowCut = eq::makeHighPass (sr, lastLowCut, 0.7071);
    }
    smMix.setTargetValue (pMix->load() * 0.01f * (pOn->load() > 0.5f ? 1.0f : 0.0f));
    smWidth.setTargetValue (pWidth->load() * 0.01f);
    smWetGain.setTargetValue (std::pow (2.0f / pDecay->load(), 0.15f));   // lange Hallzeiten nicht automatisch lauter

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const bool mono = inCh == 1;
    float pkL = 0, pkR = 0;

    for (int i = 0; i < n; ++i)
    {
        const float dl = L[i], dr = mono ? dl : R[i];
        float wl, wr;
        reverb.process (0.5f * (dl + dr), wl, wr);
        wl = lcL.process (lowCut, wl);
        wr = lcR.process (lowCut, wr);

        // Stereobreite über Mitte/Seite
        const float w = smWidth.getNextValue();
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * w;
        const float g = smWetGain.getNextValue();
        wl = (mid + side) * g;
        wr = (mid - side) * g;

        // Mix: Trockenanteil sanft absenken, Hall anheben (gleiche Leistung)
        const float m = smMix.getNextValue();
        const float dryG = std::cos (m * juce::MathConstants<float>::halfPi);
        const float wetG = std::sin (m * juce::MathConstants<float>::halfPi);
        L[i] = dl * dryG + wl * wetG;
        R[i] = dr * dryG + wr * wetG;

        vuStateL += (std::abs (L[i]) - vuStateL) * vuCoef;
        vuStateR += (std::abs (R[i]) - vuStateR) * vuCoef;
        pkL = juce::jmax (pkL, std::abs (L[i]));
        pkR = juce::jmax (pkR, std::abs (R[i]));
    }
    vuL.store (vuStateL); vuR.store (vuStateR);
    meterL.store (juce::jmax (pkL, meterL.load())); meterR.store (juce::jmax (pkR, meterR.load()));
}

void PlatteAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PlatteAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* PlatteAudioProcessor::createEditor() { return new PlatteAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PlatteAudioProcessor(); }
#endif
