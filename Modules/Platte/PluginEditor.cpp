#include "PluginEditor.h"

using namespace vintage;

//==============================================================================
PlatteAudioProcessorEditor::PlatteAudioProcessorEditor (PlatteAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addKnob ("decay",      "NACHHALL (S)", true, "0,3|0,6|1|2|4|8");
    addKnob ("mix",        "MIX (%)",      true, "0|25|50|75|100");
    addKnob ("predelay",   "PRE-DELAY",    false);
    addKnob ("damping",    juce::CharPointer_UTF8 ("D\xc3\x84MPFUNG"), false);
    addKnob ("lowcut",     "LOW CUT",      false);
    addKnob ("width",      "BREITE",       false);
    addKnob ("modulation", "MODULATION",   false);

    setupSwitch (onSwitch, "on", "AN", "AUS", lampRed);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (vu);

    rack::styleToggle (onSwitch);
    setSize (rackW, rackH);
    startTimerHz (60);
}

PlatteAudioProcessorEditor::~PlatteAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

PlatteAudioProcessorEditor::Knob& PlatteAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name, bool big, const juce::String& steps)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->big = big;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    k->slider.setMouseDragSensitivity (big ? 320 : (steps.isNotEmpty() ? 140 : 220));
    if (steps.isNotEmpty())
        k->slider.getProperties().set ("stepLabels", steps);
    addAndMakeVisible (k->slider);
    k->attachment = std::make_unique<SliderAttachment> (proc.apvts, paramID, k->slider);
    if (auto* rp = proc.apvts.getParameter (paramID))
        k->slider.setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
    knobs.push_back (std::move (k));
    return *knobs.back();
}

void PlatteAudioProcessorEditor::setupSwitch (juce::ToggleButton& b, const juce::String& paramID,
                                            const juce::String& onText, const juce::String& offText, juce::Colour lamp)
{
    b.setButtonText ("x");
    b.getProperties().set ("onText", onText);
    b.getProperties().set ("offText", offText);
    b.getProperties().set ("lamp", (int) lamp.getARGB());
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, paramID, b));
}

void PlatteAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vu.update (juce::jmax (proc.vuL.load(), proc.vuR.load()),
               juce::jmax (proc.meterL.exchange (0.0f), proc.meterR.exchange (0.0f)), dt);

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

//==============================================================================
void PlatteAudioProcessorEditor::renderPanel()
{
    panelImage = rack::renderPanel (rackW, rackH, 19);
    juce::Graphics g (panelImage);
    const juce::String titles[] = { "NACHHALL", "AUSGANG", "MISCHUNG", "KLANG" };
    for (size_t i = 0; i < sections.size(); ++i)
        rack::drawSection (g, sections[i], titles[i], lnf);
    rack::drawBezel (g, vuBezel);
    for (auto& k : knobs)
        rack::drawKnobLabel (g, k->slider, k->name, lnf, k->big ? 14.0f : 12.0f);
    rack::drawLabel (g, "HALL", switchLabel.toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawNameplate (g, nameplate, "PLATTE", "PLATTENHALL", lnf);
    rack::drawLabel (g, "PROGRAMM", presetBox.getBounds().toFloat().withY ((float) presetBox.getY() - 16.0f).withHeight (14.0f), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawTypeTag (g, rack::u8 ("TYP KB-140  \xc2\xb7  NR. 0607"), tagArea, lnf);
}

void PlatteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (panelImage, 0, 0);
}

void PlatteAudioProcessorEditor::resized()
{
    const int top = 30, bottom = rackH - 34;
    sections[0] = { 72, top, 240, 262 };
    sections[1] = { 324, top, 460, 262 };
    sections[2] = { 796, top, 234, 262 };
    sections[3] = { 72, top + 282, 958, bottom - top - 282 };

    knobs[0]->slider.setBounds (juce::Rectangle<int> (220, 220).withCentre ({ sections[0].getCentreX(), top + 132 }));
    knobs[1]->slider.setBounds (juce::Rectangle<int> (220, 220).withCentre ({ sections[2].getCentreX(), top + 132 }));
    vuBezel = juce::Rectangle<int> (380, 236).withCentre ({ sections[1].getCentreX(), top + 134 });
    vu.setBounds (vuBezel.reduced (10));

    const auto rs = sections[3];
    const int kw = 150, gap = (rs.getWidth() - 5 * kw) / 6;
    for (int i = 2; i < 7; ++i)
        knobs[(size_t) i]->slider.setBounds (rs.getX() + gap + (i - 2) * (kw + gap), rs.getY() + 18, kw, kw);

    const int colX = 1046, colW = rackW - 56 - 16 - colX;
    nameplate = { colX, top - 8, colW, 88 };
    presetBox.setBounds (colX, top + 112, colW, 28);
    switchLabel = { colX + 4, top + 168, colW, 14 };
    onSwitch.setBounds (colX + 4, top + 186, 150, 40);
    tagArea = { colX - 60, rackH - 28, colW + 60, 16 };
    renderPanel();
}
