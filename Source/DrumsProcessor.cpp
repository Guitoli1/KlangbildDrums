#include "DrumsProcessor.h"
#include "DrumsEditor.h"

#include "../Modules/Band/PluginProcessor.h"
#include "../Modules/EQ/PluginProcessor.h"
#include "../Modules/Bus/PluginProcessor.h"
#include "../Modules/Platte/PluginProcessor.h"

const std::array<DrumsAudioProcessor::ModuleInfo, 4>& DrumsAudioProcessor::moduleInfos()
{
    static const std::array<ModuleInfo, 4> m = { {
        { "band",   "BAND",   true  },
        { "eq",     "EQ",     true  },
        { "bus",    "BUS",    true  },
        { "platte", "PLATTE", false },
    } };
    return m;
}

//==============================================================================
// Drum-Presets: Module an/aus, Modul-Presets und gezielte Einstellungen
namespace
{
struct ParamSet { int module; const char* id; float value; };
struct DrumPreset
{
    const char* name;
    std::array<bool, 4> on;
    int band, bus, platte;              // Preset-Nummer im jeweiligen Modul
    std::vector<ParamSet> params;       // danach gesetzte Einzelwerte (EQ wird vorher neutral gestellt)
    float outDb = 0.0f;                 // Ausgang, damit alle Presets etwa gleich laut sind
};
constexpr int B = 0, E = 1, U = 2, P = 3;

const std::vector<DrumPreset>& drumPresets()
{
    static const std::vector<DrumPreset> p = {
        { "Standard", { true, true, true, false }, 0, 2, 0,
          { { E, "b1_on", 1 }, { E, "b1_freq", 35 }, { E, "b4_gain", -2 }, { E, "b4_q", 1.0f } }, 0.0f },
        { "Rock – Punch", { true, true, true, false }, 4, 2, 0,
          { { E, "b1_on", 1 }, { E, "b1_freq", 40 }, { E, "b2_gain", 2 }, { E, "b4_freq", 400 }, { E, "b4_gain", -3 }, { E, "b4_q", 1.2f }, { E, "b7_gain", 2 } }, 1.5f },
        { "Funk – trocken & tight", { true, true, true, true }, 2, 2, 3,
          { { E, "b1_on", 1 }, { E, "b1_freq", 50 }, { E, "b4_freq", 300 }, { E, "b4_gain", -2.5f }, { E, "b6_gain", 1.5f }, { E, "b6_q", 0.8f },
            { U, "release", 0 }, { U, "mix", 60 }, { P, "mix", 10 } }, 2.5f },
        { "Vintage 70er", { true, true, true, true }, 1, 0, 2,
          { { E, "b1_on", 1 }, { E, "b1_freq", 45 }, { E, "b3_gain", 1.5f }, { E, "b8_on", 1 }, { E, "b8_freq", 12000 },
            { U, "threshold", -16 }, { U, "mix", 60 }, { P, "mix", 12 } }, 3.5f },
        { "Parallel – fett", { true, true, true, false }, 0, 3, 0,
          { { E, "b1_on", 1 }, { E, "b1_freq", 35 } }, 2.5f },
        { "Ballade – groß", { true, true, true, true }, 0, 0, 4,
          { { E, "b1_on", 1 }, { E, "b1_freq", 35 }, { E, "b7_gain", 1.5f }, { P, "mix", 15 } }, 1.5f },
        { "Lo-Fi / Demo", { true, true, true, false }, 6, 3, 0,
          { { E, "b1_on", 1 }, { E, "b1_freq", 90 }, { E, "b5_gain", 3 }, { E, "b5_q", 0.8f }, { E, "b8_on", 1 }, { E, "b8_freq", 7000 } }, 9.5f },
        { "Snare – Einzelspur", { false, true, true, true }, 0, 0, 2,
          { { E, "b1_on", 1 }, { E, "b1_freq", 80 }, { E, "b3_gain", 2 }, { E, "b4_gain", -2 }, { E, "b6_gain", 2 },
            { U, "threshold", -18 }, { U, "mix", 100 }, { P, "mix", 20 } }, 4.0f },
        { "Raummikro – zerquetscht", { true, true, true, false }, 4, 0, 0,
          { { E, "b1_on", 1 }, { E, "b1_freq", 60 }, { E, "b4_gain", -3 },
            { U, "threshold", -35 }, { U, "ratio", 2 }, { U, "attack", 0 }, { U, "release", 0 }, { U, "makeup", 14 }, { U, "mix", 100 } }, 8.5f },
    };
    return p;
}
}

int DrumsAudioProcessor::getNumPrograms() { return (int) drumPresets().size(); }

const juce::String DrumsAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) drumPresets().size()) ? juce::String (juce::CharPointer_UTF8 (drumPresets()[(size_t) index].name)) : juce::String();
}

void DrumsAudioProcessor::setModuleParam (int module, const juce::String& id, float value)
{
    for (auto* prm : modules[(size_t) module].processor->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->paramID == id)
            {
                rp->setValueNotifyingHost (rp->convertTo0to1 (value));
                return;
            }
}

void DrumsAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) drumPresets().size()))
        return;
    currentProgram = index;
    const auto& d = drumPresets()[(size_t) index];
    for (int i = 0; i < numModules; ++i)
        if (auto* prm = apvts.getParameter (onParamID (i)))
            prm->setValueNotifyingHost (d.on[(size_t) i] ? 1.0f : 0.0f);

    modules[B].processor->setCurrentProgram (d.band);
    modules[U].processor->setCurrentProgram (d.bus);
    modules[P].processor->setCurrentProgram (d.platte);
    // EQ neutral stellen (Standardwerte), dann die Preset-Werte setzen
    for (auto* prm : modules[E].processor->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (rp->paramID != "analyzer")
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (const auto& ps : d.params)
        setModuleParam (ps.module, ps.id, ps.value);
    if (auto* prm = apvts.getParameter ("out"))
        prm->setValueNotifyingHost (prm->convertTo0to1 (d.outDb));
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumsAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (int i = 0; i < numModules; ++i)
    {
        const auto& mi = moduleInfos()[(size_t) i];
        const juce::String title = juce::String (mi.title).substring (0, 1) + juce::String (mi.title).substring (1).toLowerCase();
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { onParamID (i), 1 }, title + " an", mi.defaultOn));
    }
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Ausgang",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

DrumsAudioProcessor::DrumsAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KlangbildDrums", createLayout())
{
    modules[0].processor = std::make_unique<BandAudioProcessor>();
    modules[1].processor = std::make_unique<KlangbildAudioProcessor>();
    modules[2].processor = std::make_unique<BusAudioProcessor>();
    modules[3].processor = std::make_unique<PlatteAudioProcessor>();

    for (int i = 0; i < numModules; ++i)
    {
        modules[(size_t) i].on = apvts.getRawParameterValue (onParamID (i));
        modules[(size_t) i].processor->setPlayConfigDetails (2, 2, sr, maxBlock);
    }
    pOut = apvts.getRawParameterValue ("out");
    setCurrentProgram (0);
    startTimerHz (4);
}

DrumsAudioProcessor::~DrumsAudioProcessor()
{
    stopTimer();
}

bool DrumsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void DrumsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);
    dry.setSize (2, maxBlock);
    for (auto& m : modules)
    {
        m.processor->setPlayConfigDetails (2, 2, sr, maxBlock);
        m.processor->prepareToPlay (sr, maxBlock);
        m.mix.reset (sr, 0.02);
        m.mix.setCurrentAndTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);
    }
    smOut.reset (sr, 0.03);
    smOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    vuCoef = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
    vuStateL = vuStateR = 0;
    timerCallback();
}

void DrumsAudioProcessor::releaseResources()
{
    for (auto& m : modules)
        m.processor->releaseResources();
}

void DrumsAudioProcessor::timerCallback()
{
    int latency = 0;
    for (auto& m : modules)
        if (m.on->load() > 0.5f)
            latency += m.processor->getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

void DrumsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || getTotalNumInputChannels() == 0)
        return;

    // Mono-Eingang auf beide Kanäle verteilen; die Module arbeiten immer in Stereo
    if (getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    juce::AudioBuffer<float> stereo (buffer.getArrayOfWritePointers(), 2, n);
    if (dry.getNumSamples() < n)
        dry.setSize (2, n, false, false, true);

    for (auto& m : modules)
    {
        m.processor->setPlayHead (getPlayHead());
        m.mix.setTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);

        if (! m.mix.isSmoothing())
        {
            if (m.mix.getCurrentValue() < 0.5f)
                continue;                       // Modul aus: wird gar nicht gerechnet
            midi.clear();
            m.processor->processBlock (stereo, midi);
            continue;
        }

        // Beim Ein-/Ausschalten kurz überblenden, damit nichts knackt
        for (int ch = 0; ch < 2; ++ch)
            dry.copyFrom (ch, 0, stereo, ch, 0, n);
        midi.clear();
        m.processor->processBlock (stereo, midi);
        for (int i = 0; i < n; ++i)
        {
            const float g = m.mix.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = stereo.getWritePointer (ch);
                const float x = dry.getSample (ch, i);
                d[i] = x + g * (d[i] - x);
            }
        }
    }

    smOut.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    float pkL = 0, pkR = 0;
    float* L = stereo.getWritePointer (0);
    float* R = stereo.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float g = smOut.getNextValue();
        L[i] *= g;
        R[i] *= g;
        vuStateL += (std::abs (L[i]) - vuStateL) * vuCoef;
        vuStateR += (std::abs (R[i]) - vuStateR) * vuCoef;
        pkL = juce::jmax (pkL, std::abs (L[i]));
        pkR = juce::jmax (pkR, std::abs (R[i]));
    }
    vuL.store (vuStateL); vuR.store (vuStateR);
    meterL.store (juce::jmax (pkL, meterL.load())); meterR.store (juce::jmax (pkR, meterR.load()));
}

//==============================================================================
void DrumsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("KlangbildDrumsState");
    root.setAttribute ("version", 1);
    root.setAttribute ("program", currentProgram);
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    for (int i = 0; i < numModules; ++i)
    {
        juce::MemoryBlock mb;
        modules[(size_t) i].processor->getStateInformation (mb);
        auto* e = root.createNewChildElement ("Module");
        e->setAttribute ("id", moduleInfos()[(size_t) i].id);
        e->addTextElement (mb.toBase64Encoding());
    }
    copyXmlToBinary (root, destData);
}

void DrumsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("KlangbildDrumsState"))
        return;
    currentProgram = juce::jlimit (0, getNumPrograms() - 1, root->getIntAttribute ("program", 0));

    if (auto* params = root->getChildByName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*params));
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                rp->setValueNotifyingHost (rp->convertTo0to1 (rp->convertFrom0to1 (rp->getValue())));
    }

    for (auto* e : root->getChildWithTagNameIterator ("Module"))
    {
        const auto id = e->getStringAttribute ("id");
        for (int i = 0; i < numModules; ++i)
            if (id == moduleInfos()[(size_t) i].id)
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (e->getAllSubText()) && mb.getSize() > 0)
                    modules[(size_t) i].processor->setStateInformation (mb.getData(), (int) mb.getSize());
            }
    }
    for (auto& m : modules)
        m.mix.setCurrentAndTargetValue (m.on->load() > 0.5f ? 1.0f : 0.0f);
}

juce::AudioProcessorEditor* DrumsAudioProcessor::createEditor() { return new DrumsAudioProcessorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DrumsAudioProcessor(); }
