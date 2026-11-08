#include "PluginEditor.h"

using namespace vintage;

//==============================================================================
BandAudioProcessorEditor::BandAudioProcessorEditor (BandAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    addKnob ("input",   "EINGANG (DB)", true, juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "12|\xe2\x88\x92" "6|0|+6|+12|+18")));
    addKnob ("output",  "AUSGANG (DB)", true, juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92" "18|\xe2\x88\x92" "12|\xe2\x88\x92" "6|0|+6|+12")));
    addKnob ("speed",   "SPEED (IPS)",  false, "7,5|15|30");
    addKnob ("bias",    "BIAS",         false, juce::String (juce::CharPointer_UTF8 ("UNTER|\xe2\x80\x93|0|\xe2\x80\x93|\xc3\x9c" "BER")));
    addKnob ("wow",     "WOW (%)",      false, "0|25|50|75|100");
    addKnob ("flutter", "FLUTTER (%)",  false, "0|25|50|75|100");
    addKnob ("hiss",    "RAUSCHEN (%)", false, "0|25|50|75|100");

    setupSwitch (onSwitch, "on", "AN", "AUS", lampRed);
    setupSwitch (autoSwitch, "autogain", "AN", "AUS", lampAmber);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);
    reels.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (reels);

    rack::styleToggle (onSwitch);
    rack::styleToggle (autoSwitch);
    setSize (rackW, rackH);
    startTimerHz (60);
}

BandAudioProcessorEditor::~BandAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

BandAudioProcessorEditor::Knob& BandAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& name, bool big, const juce::String& steps)
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

void BandAudioProcessorEditor::setupSwitch (juce::ToggleButton& b, const juce::String& paramID,
                                            const juce::String& onText, const juce::String& offText, juce::Colour lamp)
{
    b.setButtonText ("x");
    b.getProperties().set ("onText", onText);
    b.getProperties().set ("offText", offText);
    b.getProperties().set ("lamp", (int) lamp.getARGB());
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (proc.apvts, paramID, b));
}

void BandAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.peakL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.peakR.exchange (0.0f), dt);

    // Spulen drehen sich nur, solange Audio durch das Band läuft
    const auto s = proc.samplesRun.load();
    if (s != lastSamples)
    {
        lastSamples = s;
        const float ips[] = { 7.5f, 15.0f, 30.0f };
        const int sp = juce::jlimit (0, 2, (int) proc.apvts.getRawParameterValue ("speed")->load());
        const float step = dt * ips[sp] * 0.09f * juce::MathConstants<float>::twoPi;
        reels.angleL = std::fmod (reels.angleL + step, juce::MathConstants<float>::twoPi);
        reels.angleR = std::fmod (reels.angleR + step * Reels::packL / Reels::packR, juce::MathConstants<float>::twoPi);
        reels.repaint();
    }

    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
}

//==============================================================================
void BandAudioProcessorEditor::renderPanel()
{
    panelImage = rack::renderPanel (rackW, rackH, 25);
    juce::Graphics g (panelImage);
    const juce::String titles[] = { "EINGANG", "BANDLAUF", "AUSGANG", "BAND" };
    for (size_t i = 0; i < sections.size(); ++i)
        rack::drawSection (g, sections[i], titles[i], lnf);
    rack::drawBezel (g, vuBezelL);
    rack::drawBezel (g, vuBezelR);
    for (auto& k : knobs)
        rack::drawKnobLabel (g, k->slider, k->name, lnf, k->big ? 14.0f : 12.0f);
    const juce::String names[] = { "BAND", "AUTO-GAIN" };
    for (size_t i = 0; i < 2; ++i)
        rack::drawLabel (g, names[i], switchLabels[i].toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawNameplate (g, nameplate, "BAND", "BANDMASCHINE", lnf);
    rack::drawLabel (g, "PROGRAMM", presetBox.getBounds().toFloat().withY ((float) presetBox.getY() - 16.0f).withHeight (14.0f), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawTypeTag (g, rack::u8 ("TYP KB-1500  \xc2\xb7  NR. 0610"), tagArea, lnf);
}

void BandAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (panelImage, 0, 0);
}

void BandAudioProcessorEditor::resized()
{
    const int top = 30, bottom = rackH - 34;
    sections[0] = { 72, top, 240, 330 };
    sections[1] = { 324, top, 460, 330 };
    sections[2] = { 796, top, 234, 330 };
    sections[3] = { 72, top + 350, 958, bottom - top - 350 };

    knobs[0]->slider.setBounds (juce::Rectangle<int> (220, 220).withCentre ({ sections[0].getCentreX(), top + 160 }));
    knobs[1]->slider.setBounds (juce::Rectangle<int> (220, 220).withCentre ({ sections[2].getCentreX(), top + 160 }));
    reels.setBounds (sections[1].getX() + 10, top + 14, sections[1].getWidth() - 20, 166);
    vuBezelL = { sections[1].getX() + 14, top + 186, 210, 136 };
    vuBezelR = { sections[1].getRight() - 14 - 210, top + 186, 210, 136 };
    vuLeft.setBounds (vuBezelL.reduced (8));
    vuRight.setBounds (vuBezelR.reduced (8));

    const auto rs = sections[3];
    const int kw = 150, gap = (rs.getWidth() - 5 * kw) / 6;
    for (int i = 2; i < 7; ++i)
        knobs[(size_t) i]->slider.setBounds (rs.getX() + gap + (i - 2) * (kw + gap), rs.getY() + 18, kw, kw);

    const int colX = 1046, colW = rackW - 56 - 16 - colX;
    nameplate = { colX, top - 8, colW, 88 };
    presetBox.setBounds (colX, top + 112, colW, 28);
    juce::ToggleButton* sws[] = { &onSwitch, &autoSwitch };
    for (int i = 0; i < 2; ++i)
    {
        const int y = top + 168 + i * 84;
        switchLabels[(size_t) i] = { colX + 4, y, colW, 14 };
        sws[i]->setBounds (colX + 4, y + 18, 150, 40);
    }
    tagArea = { colX - 60, rackH - 28, colW + 60, 16 };
    renderPanel();
}

//==============================================================================
void BandAudioProcessorEditor::Reels::drawReel (juce::Graphics& g, juce::Point<float> c, float radius, float packRadius, float angle)
{
    // Schatten
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c.translated (3.0f, 5.0f)));
    // Bandwickel (braunes Band)
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a3a24), c.x, c.y - packRadius, juce::Colour (0xff2a190e), c.x, c.y + packRadius, false));
    g.fillEllipse (juce::Rectangle<float> (packRadius * 2, packRadius * 2).withCentre (c));
    g.setColour (juce::Colour (0x22ffffff));
    for (float rr = radius * 0.3f; rr < packRadius; rr += 3.0f)
        g.drawEllipse (juce::Rectangle<float> (rr * 2, rr * 2).withCentre (c), 0.6f);

    // Metallspule mit drei Aussparungen
    juce::Path flange;
    flange.addEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (c));
    for (int k = 0; k < 3; ++k)
    {
        const float a0 = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
        juce::Path hole;
        hole.addCentredArc (c.x, c.y, radius * 0.80f, radius * 0.80f, 0.0f, a0 + 0.25f, a0 + 1.65f, true);
        hole.addCentredArc (c.x, c.y, radius * 0.36f, radius * 0.36f, 0.0f, a0 + 1.50f, a0 + 0.42f, false);
        hole.closeSubPath();
        flange.addPath (hole);
    }
    flange.setUsingNonZeroWinding (false);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xe0d9dcdf), c.x - radius, c.y - radius, juce::Colour (0xe07d8287), c.x + radius, c.y + radius, false));
    g.fillPath (flange);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (flange, juce::PathStrokeType (1.0f));

    // Nabe
    g.setColour (juce::Colour (0xff3a3632));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.34f, radius * 0.34f).withCentre (c));
    g.setColour (juce::Colour (0xffb9bcc0));
    for (int k = 0; k < 3; ++k)
    {
        const float a = angle + (float) k * juce::MathConstants<float>::twoPi / 3.0f;
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c.getPointOnCircumference (radius * 0.1f, a)));
    }
}

void BandAudioProcessorEditor::Reels::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float radius = reelRadius;
    const juce::Point<float> cl (b.getX() + 90.0f, b.getCentreY() - 4.0f), cr (b.getRight() - 90.0f, b.getCentreY() - 4.0f);

    // Bandweg unten über den Tonkopf
    const juce::Point<float> head (b.getCentreX(), b.getBottom() - 10.0f);
    juce::Path tape;
    tape.startNewSubPath (cl.x, cl.y + packL);
    tape.lineTo (head.x - 26.0f, head.y);
    tape.lineTo (head.x + 26.0f, head.y);
    tape.lineTo (cr.x, cr.y + packR);
    g.setColour (juce::Colour (0xff4a2f1c));
    g.strokePath (tape, juce::PathStrokeType (3.0f));
    // Tonkopf
    const auto hr = juce::Rectangle<float> (36.0f, 16.0f).withCentre (head.translated (0.0f, 6.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd5d7da), hr.getX(), hr.getY(), juce::Colour (0xff6d7175), hr.getX(), hr.getBottom(), false));
    g.fillRoundedRectangle (hr, 4.0f);

    // Wickelteller laufen mit gleicher Bandgeschwindigkeit -> kleiner Wickel dreht schneller
    drawReel (g, cl, radius, packL, angleL);
    drawReel (g, cr, radius, packR, angleR);
}
