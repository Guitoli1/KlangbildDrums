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

    presetBox.getProperties().set ("plate", true);
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (vu);

    setSize (1000, 620);
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
void PlatteAudioProcessorEditor::renderTextures()
{
    tolexImage = juce::Image (juce::Image::RGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), false);
    {
        juce::Image::BitmapData data (tolexImage, juce::Image::BitmapData::writeOnly);
        juce::Random rnd (7);
        for (int y = 0; y < tolexImage.getHeight(); ++y)
            for (int x = 0; x < tolexImage.getWidth(); ++x)
            {
                const int weave = ((x + y) % 4 == 0 || (x - y + 4096) % 4 == 0) ? -5 : 0;
                const int n = rnd.nextInt (13) - 6 + weave;
                data.setPixelColour (x, y, juce::Colour ((juce::uint8) juce::jlimit (0, 255, 34 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 28 + n),
                                                         (juce::uint8) juce::jlimit (0, 255, 24 + n)));
            }
    }
    if (faceplate.isEmpty())
        return;
    faceplateImage = juce::Image (juce::Image::ARGB, faceplate.getWidth(), faceplate.getHeight(), true);
    juce::Graphics g (faceplateImage);
    const auto r = faceplateImage.getBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (cream.brighter (0.06f), 0, 0, creamDark, 0, r.getHeight(), false));
    g.fillRoundedRectangle (r, 6.0f);
    juce::Random rnd (3);
    for (int y = 0; y < faceplate.getHeight(); ++y)
    {
        g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colour (0xff6b5534)).withAlpha (0.015f + rnd.nextFloat() * 0.035f));
        g.drawHorizontalLine (y, 0.0f, r.getWidth());
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                                             juce::Colour (0x30694a1c), 0, 0, true));
    g.fillRoundedRectangle (r, 6.0f);
}

void PlatteAudioProcessorEditor::drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                                             juce::Font f, juce::Justification j)
{
    g.setFont (f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawFittedText (text, r.translated (0.0f, 1.0f).toNearestInt(), j, 2, 1.0f);
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt(), j, 2, 1.0f);
}

void PlatteAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (tolexImage, 0, 0);

    // Logo
    const auto scriptF = lnf.scriptFont (64.0f);
    const auto logoArea = juce::Rectangle<float> (40.0f, 8.0f, 400.0f, 84.0f);
    g.setFont (scriptF);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawText ("Klangbild", logoArea.translated (2.0f, 3.0f), juce::Justification::centredLeft);
    g.setColour (logo);
    g.drawText ("Klangbild", logoArea, juce::Justification::centredLeft);
    const float sw = (float) juce::GlyphArrangement::getStringWidthInt (scriptF, "Klangbild");
    g.setColour (gold);
    g.setFont (lnf.labelFont (26.0f, 0.35f));
    g.drawText ("PLATTE", juce::Rectangle<float> (logoArea.getX() + sw + 14.0f, logoArea.getY() + 30.0f, 140.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (logo.withAlpha (0.55f));
    g.setFont (lnf.labelFont (12.0f, 0.3f));
    g.drawText (juce::CharPointer_UTF8 ("PLATTENHALL \xc2\xb7 GESANG \xc2\xb7 SNARE \xc2\xb7 GITARRE"), juce::Rectangle<float> (44.0f, 90.0f, 420.0f, 16.0f), juce::Justification::centredLeft);

    const auto pa = presetArea.toFloat();
    g.setColour (logo.withAlpha (0.6f));
    g.setFont (lnf.labelFont (10.0f, 0.3f));
    g.drawText ("PROGRAMM", pa.withY (pa.getY() - 15.0f).withHeight (13.0f), juce::Justification::centredLeft);

    // Frontplatte
    const auto fp = faceplate.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (fp.expanded (4.0f).translated (0, 2.0f), 9.0f);
    g.setGradientFill (juce::ColourGradient (gold.brighter (0.3f), 0, fp.getY(), gold.darker (0.6f), 0, fp.getBottom(), false));
    g.fillRoundedRectangle (fp.expanded (3.0f), 8.0f);
    g.drawImageAt (faceplateImage, faceplate.getX(), faceplate.getY());
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (fp, 6.0f, 1.0f);
    for (auto [x, y, a] : { std::tuple { fp.getX() + 12.0f, fp.getY() + 12.0f, 0.4f }, std::tuple { fp.getRight() - 12.0f, fp.getY() + 12.0f, 1.3f },
                            std::tuple { fp.getX() + 12.0f, fp.getBottom() - 12.0f, 2.2f }, std::tuple { fp.getRight() - 12.0f, fp.getBottom() - 12.0f, 0.9f } })
        VintageLookAndFeel::drawScrew (g, { x, y }, 5.0f, a);

    // Rahmen um das große VU-Meter
    const auto vb = vuBezel.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (vb.translated (2.0f, 3.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2520), 0, vb.getY(), juce::Colour (0xff120f0c), 0, vb.getBottom(), false));
    g.fillRoundedRectangle (vb, 12.0f);

    // Trennlinie zwischen Haupt- und Nebenreglern
    for (int dy : { 0, 1 })
    {
        g.setColour (dy == 0 ? ink.withAlpha (0.4f) : juce::Colours::white.withAlpha (0.5f));
        g.drawHorizontalLine (dividerArea.getY() + dy, (float) dividerArea.getX(), (float) dividerArea.getRight());
    }

    for (auto& k : knobs)
    {
        const auto sb = k->slider.getBounds().toFloat();
        drawEngraved (g, k->name, { sb.getX() - 20.0f, sb.getBottom() - 4.0f, sb.getWidth() + 40.0f, k->big ? 24.0f : 18.0f },
                      lnf.labelFont (k->big ? 18.0f : 13.0f, k->big ? 0.2f : 0.14f), juce::Justification::centred);
    }

    drawEngraved (g, "HALL", switchLabel.toFloat(), lnf.labelFont (12.0f, 0.14f), juce::Justification::centred);

    const auto tag = tagArea.toFloat();
    g.setColour (ink.withAlpha (0.85f));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (gold);
    g.setFont (lnf.labelFont (11.0f, 0.2f));
    g.drawText ("MODELL KB-140", tag, juce::Justification::centred);
}

void PlatteAudioProcessorEditor::resized()
{
    const int W = getWidth();
    presetArea = { 44, 128, 260, 32 };
    presetBox.setBounds (presetArea);

    faceplate = { 24, 178, W - 48, 420 };
    const auto in = faceplate.reduced (30, 20);

    // obere Reihe: Nachhall | VU | Mix
    const int topH = 250;
    knobs[0]->slider.setBounds (in.getX(), in.getY() + 10, 250, 210);
    knobs[1]->slider.setBounds (in.getRight() - 250, in.getY() + 10, 250, 210);
    vuBezel = juce::Rectangle<int> (360, 232).withCentre ({ in.getCentreX(), in.getY() + 122 });
    vu.setBounds (vuBezel.reduced (10));
    dividerArea = { in.getX() + 10, in.getY() + topH + 6, in.getWidth() - 20, 2 };

    // untere Reihe: Pre-Delay, Dämpfung, Low Cut, Breite, Modulation | Schalter
    const int rowY = in.getY() + topH + 14;
    for (int i = 2; i < 7; ++i)
        knobs[(size_t) i]->slider.setBounds (in.getX() + (i - 2) * 116, rowY, 116, 108);

    const int restX = in.getX() + 5 * 116, cx = (restX + in.getRight()) / 2;
    switchLabel = { cx - 65, rowY + 8, 130, 16 };
    onSwitch.setBounds (cx - 60, rowY + 30, 120, 40);
    tagArea = { cx - 100, rowY + 84, 200, 24 };

    renderTextures();
}
