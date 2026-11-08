#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

Attr unit (const char* u, int decimals, bool sign = false)
{
    return Attr().withStringFromValueFunction ([=] (float v, int)
                 {
                     auto s = juce::String (std::abs (v) < 0.05f && decimals > 0 ? 0.0f : v, decimals).replaceCharacter ('.', ',');
                     if (sign && v > 0.049f) s = "+" + s;
                     return s + " " + u;
                 })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.replaceCharacter (',', '.').getFloatValue(); });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout BandAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<APF> (juce::ParameterID { "input", 1 }, "Eingang", juce::NormalisableRange<float> (-12.0f, 18.0f, 0.1f), 0.0f, unit ("dB", 1, true)));
    l.add (std::make_unique<APF> (juce::ParameterID { "output", 1 }, "Ausgang", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, unit ("dB", 1, true)));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "speed", 1 }, "Bandgeschwindigkeit",
                                                         juce::StringArray { "19 cm/s (7,5 ips)", "38 cm/s (15 ips)", "76 cm/s (30 ips)" }, 1));
    l.add (std::make_unique<APF> (juce::ParameterID { "bias", 1 }, "Bias", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, unit ("%", 0, true)));
    l.add (std::make_unique<APF> (juce::ParameterID { "wow", 1 }, "Wow", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, unit ("%", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "flutter", 1 }, "Flutter", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, unit ("%", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "hiss", 1 }, "Rauschen", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, unit ("%", 0)));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "on", 1 }, "Band an", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autogain", 1 }, "Auto-Gain", true));
    return l;
}

const std::vector<BandAudioProcessor::Preset>& BandAudioProcessor::presets()
{
    static const std::vector<Preset> p = {
        { "Standard (15 ips)",      { { "input", 0 },  { "output", 0 }, { "speed", 1 }, { "bias", 0 },   { "wow", 15 }, { "flutter", 15 }, { "hiss", 0 },  { "autogain", 1 } } },
        { "Warm (7,5 ips)",         { { "input", 4 },  { "output", 0 }, { "speed", 0 }, { "bias", 0 },   { "wow", 25 }, { "flutter", 20 }, { "hiss", 10 }, { "autogain", 1 } } },
        { "Studio (30 ips)",        { { "input", 0 },  { "output", 0 }, { "speed", 2 }, { "bias", 10 },  { "wow", 5 },  { "flutter", 5 },  { "hiss", 0 },  { "autogain", 1 } } },
        { "Summe veredeln",         { { "input", 2 },  { "output", 0 }, { "speed", 1 }, { "bias", 20 },  { "wow", 5 },  { "flutter", 8 },  { "hiss", 0 },  { "autogain", 1 } } },
        { "Drums anfahren",         { { "input", 9 },  { "output", 0 }, { "speed", 1 }, { "bias", -30 }, { "wow", 10 }, { "flutter", 10 }, { "hiss", 0 },  { "autogain", 1 } } },
        { "Gitarre & Bass dick",    { { "input", 6 },  { "output", 0 }, { "speed", 0 }, { "bias", -10 }, { "wow", 10 }, { "flutter", 12 }, { "hiss", 0 },  { "autogain", 1 } } },
        { "Lo-Fi Demo-Band",        { { "input", 7 },  { "output", 0 }, { "speed", 0 }, { "bias", -45 }, { "wow", 70 }, { "flutter", 60 }, { "hiss", 45 }, { "autogain", 1 } } },
    };
    return p;
}

void BandAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size()))
        return;
    currentProgram = index;
    for (auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
}

const juce::String BandAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size())
               ? juce::String (juce::CharPointer_UTF8 (presets()[(size_t) index].name)) : juce::String();
}

BandAudioProcessor::BandAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildBand", createLayout())
{
    pIn = apvts.getRawParameterValue ("input");     pOut = apvts.getRawParameterValue ("output");
    pSpeed = apvts.getRawParameterValue ("speed");  pBias = apvts.getRawParameterValue ("bias");
    pWow = apvts.getRawParameterValue ("wow");      pFlutter = apvts.getRawParameterValue ("flutter");
    pHiss = apvts.getRawParameterValue ("hiss");    pOn = apvts.getRawParameterValue ("on");
    pAuto = apvts.getRawParameterValue ("autogain");
}

bool BandAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == out;
}

void BandAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) maxBlock);
    work.setSize (2, maxBlock);
    machine.prepare (sr * 2.0);
    latency = (int) std::lround (machine.latencySamples() / 2.0 + (double) oversampler->getLatencyInSamples()) + 1;   // +1: Laufzeit der Entzerrer
    setLatencySamples (latency);
    for (auto& d : dryDelay) d.assign ((size_t) latency + 1, 0.0f);
    dryPos = 0;
    smIn.reset (sr, 0.03);  smIn.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pIn->load()));
    smOut.reset (sr, 0.03); smOut.setCurrentAndTargetValue (1.0f);
    smOn.reset (sr, 0.03);  smOn.setCurrentAndTargetValue (pOn->load() > 0.5f ? 1.0f : 0.0f);
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
}

void BandAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int ch = buffer.getNumChannels();
    if (ch == 0 || oversampler == nullptr)
        return;

    const float inDb = pIn->load();
    machine.setParams ((int) pSpeed->load(), pBias->load() * 0.01f, pWow->load() * 0.01f,
                       pFlutter->load() * 0.01f, pHiss->load() * 0.01f);
    smIn.setTargetValue (juce::Decibels::decibelsToGain (inDb));
    smOut.setTargetValue (juce::Decibels::decibelsToGain (pOut->load() - (pAuto->load() > 0.5f ? inDb : 0.0f)));
    smOn.setTargetValue (pOn->load() > 0.5f ? 1.0f : 0.0f);

    float* L = buffer.getWritePointer (0);
    float* R = ch > 1 ? buffer.getWritePointer (1) : nullptr;
    float pkL = 0, pkR = 0;
    const int dlen = latency + 1;

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - start);
        float* wl = work.getWritePointer (0);
        float* wr = work.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float g = smIn.getNextValue();
            wl[i] = L[start + i] * g;
            wr[i] = (R != nullptr ? R[start + i] : L[start + i]) * g;
        }

        // Band-Modell mit doppelter Abtastrate (weniger Aliasing, saubere Höhen)
        juce::dsp::AudioBlock<float> block (work.getArrayOfWritePointers(), 2, (size_t) n);
        auto up = oversampler->processSamplesUp (block);
        float* ul = up.getChannelPointer (0);
        float* ur = up.getChannelPointer (1);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            machine.process (ul[i], ur[i]);
        oversampler->processSamplesDown (block);

        for (int i = 0; i < n; ++i)
        {
            const int k = start + i;
            const float dl = L[k], dr = R != nullptr ? R[k] : dl;
            const float go = smOut.getNextValue(), on = smOn.getNextValue();
            const float pl = wl[i] * go, pr = wr[i] * go;

            // trockenes Signal um die Latenz verzögern, damit der Bypass ohne Versatz umschaltet
            dryDelay[0][(size_t) dryPos] = dl; dryDelay[1][(size_t) dryPos] = dr;
            const int rp = (dryPos + 1) % dlen;
            const float ddl = dryDelay[0][(size_t) rp], ddr = dryDelay[1][(size_t) rp];
            dryPos = rp;

            const float ol = on >= 1.0f ? pl : (on <= 0.0f ? ddl : ddl + (pl - ddl) * on);
            const float orr = on >= 1.0f ? pr : (on <= 0.0f ? ddr : ddr + (pr - ddr) * on);
            L[k] = ol;
            if (R != nullptr) R[k] = orr;

            vuStateL += (std::abs (ol) - vuStateL) * vuCoef;
            vuStateR += (std::abs (orr) - vuStateR) * vuCoef;
            pkL = juce::jmax (pkL, std::abs (ol));
            pkR = juce::jmax (pkR, std::abs (orr));
        }
    }
    vuL.store (vuStateL); vuR.store (vuStateR);
    peakL.store (juce::jmax (pkL, peakL.load())); peakR.store (juce::jmax (pkR, peakR.load()));
    if (pOn->load() > 0.5f)
        samplesRun.fetch_add (total);
}

void BandAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BandAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
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

juce::AudioProcessorEditor* BandAudioProcessor::createEditor() { return new BandAudioProcessorEditor (*this); }

#ifndef KB_EMBEDDED
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BandAudioProcessor(); }
#endif
