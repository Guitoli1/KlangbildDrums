#include "PluginEditor.h"

using namespace vintage;

namespace
{
constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
constexpr float eqRangeDb = 24.0f;
constexpr float analyzerTopDb = 0.0f, analyzerBottomDb = -90.0f;
constexpr float tiltDbPerOct = 3.0f;   // Neigung, damit Musik/Rosa Rauschen eher flach erscheint
}

// Farbzuordnung für den Röhrenbildschirm
namespace theme
{
const juce::Colour grid         = crt::grid;
const juce::Colour gridStrong   = crt::gridStrong;
const juce::Colour textDim      = crt::label;
const juce::Colour text         = crt::phosphor;
const juce::Colour curve        = crt::curve;
const juce::Colour preSpectrum  = crt::phosphor.darker (1.4f);
const juce::Colour postSpectrum = crt::postSpectrum;
const juce::Colour panel        { 0xff07110a };
const juce::Colour display      = crt::screenCentre;
inline juce::Colour band (int i) { return crt::band (i); }
}

//==============================================================================
ResponseDisplay::ResponseDisplay (KlangbildAudioProcessor& p, VintageLookAndFeel& l) : proc (p), lnf (l)
{
    setOpaque (false);
    startTimerHz (30);
}

ResponseDisplay::~ResponseDisplay() { stopTimer(); }

juce::Rectangle<float> ResponseDisplay::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (8).withTrimmedRight (36).withTrimmedTop (10).withTrimmedBottom (22);
}

float ResponseDisplay::freqToX (float f) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (f / minFreq) / std::log (maxFreq / minFreq);
}

float ResponseDisplay::xToFreq (float x) const
{
    const auto a = plotArea();
    return minFreq * std::pow (maxFreq / minFreq, (x - a.getX()) / a.getWidth());
}

float ResponseDisplay::gainToY (float g) const
{
    const auto a = plotArea();
    return a.getCentreY() - (g / eqRangeDb) * a.getHeight() * 0.5f;
}

float ResponseDisplay::yToGain (float y) const
{
    const auto a = plotArea();
    return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * eqRangeDb;
}

float ResponseDisplay::analyzerDbToY (float db) const
{
    const auto a = plotArea();
    return juce::jmap (db, analyzerBottomDb, analyzerTopDb, a.getBottom(), a.getY());
}

juce::Point<float> ResponseDisplay::nodePosition (int band) const
{
    const auto s = proc.getBandSettings (band);
    const bool cut = eq::isCut (eq::typeForBand (band));
    return { freqToX (s.freq), gainToY (cut ? 0.0f : juce::jlimit (-eqRangeDb, eqRangeDb, s.gain)) };
}

int ResponseDisplay::hitTestNode (juce::Point<float> p) const
{
    // das ausgewählte Band hat Vorrang, falls Punkte übereinander liegen
    if (selected >= 0 && nodePosition (selected).getDistanceFrom (p) < 12.0f)
        return selected;
    int best = -1;
    float bestDist = 12.0f;
    for (int b = 0; b < eq::numBands; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

void ResponseDisplay::setParam (const juce::String& id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

//==============================================================================
void ResponseDisplay::resized()
{
    const auto n = (size_t) juce::jmax (2, (int) plotArea().getWidth() + 1);
    preDb.assign (n, -120.0f);
    postDb.assign (n, -120.0f);
    totalCurveDb.assign (n, 0.0f);
    selectedCurveDb.assign (n, 0.0f);
    updateCurves();
    renderOverlay();
}

void ResponseDisplay::renderOverlay()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    overlay = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (overlay);
    const auto b = getLocalBounds().toFloat();

    // Zeilenraster der Bildröhre
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (int y = 0; y < getHeight(); y += 3)
        g.drawHorizontalLine (y, 0.0f, b.getWidth());

    // Vignette
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, b.getCentreX(), b.getCentreY(),
                                             juce::Colours::black.withAlpha (0.6f), b.getX(), b.getY(), true));
    g.fillRect (b);

    // Glasreflex
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f), b.getX(), b.getY(),
                                             juce::Colours::white.withAlpha (0.0f), b.getX() + b.getWidth() * 0.35f, b.getY() + b.getHeight() * 0.5f, false));
    g.fillRoundedRectangle (b.reduced (6.0f), 16.0f);

    // abgerundete Ecken der Röhre
    juce::Path corners;
    corners.addRectangle (b);
    corners.addRoundedRectangle (b, 18.0f);
    corners.setUsingNonZeroWinding (false);
    g.setColour (juce::Colour (0xff0b0a09));
    g.fillPath (corners);
}

void ResponseDisplay::paintOverChildren (juce::Graphics& g)
{
    g.drawImageAt (overlay, 0, 0);
}

void ResponseDisplay::updateCurves()
{
    const double sr = proc.currentSampleRate.load();
    const auto a = plotArea();
    const size_t n = totalCurveDb.size();

    std::array<eq::BandCoeffs, eq::numBands> bc;
    std::array<bool, eq::numBands> on {};
    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto s = proc.getBandSettings (b);
        on[(size_t) b] = s.on;
        bc[(size_t) b] = eq::makeBand (eq::typeForBand (b), sr, s.freq, s.gain, s.q, s.slope);
    }

    for (size_t i = 0; i < n; ++i)
    {
        const double f = xToFreq (a.getX() + (float) i);
        double total = 0.0;
        for (int b = 0; b < eq::numBands; ++b)
        {
            if (! on[(size_t) b] && b != selected)
                continue;
            const double db = f < sr * 0.5 ? eq::magnitudeDb (bc[(size_t) b], f, sr) : -120.0;
            if (on[(size_t) b]) total += db;
            if (b == selected)  selectedCurveDb[i] = (float) db;
        }
        totalCurveDb[i] = (float) total;
    }
}

void ResponseDisplay::processSpectrum (std::vector<float>& smoothed, bool gotNewData)
{
    if (! gotNewData)
    {
        for (auto& v : smoothed)
            v = juce::jmax (-120.0f, v - 1.5f);   // langsam ausblenden, wenn kein Audio kommt
        return;
    }

    const double sr = proc.currentSampleRate.load();
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const float norm = 4.0f / (float) fftSize;   // Hann-Fenster (0.5) + einseitiges Spektrum
    const int maxBin = fftSize / 2;
    const auto a = plotArea();

    for (size_t i = 0; i < smoothed.size(); ++i)
    {
        const double f0 = xToFreq (a.getX() + (float) i - 0.5f);
        const double f1 = xToFreq (a.getX() + (float) i + 0.5f);
        const double b0 = f0 * fftSize / sr, b1 = f1 * fftSize / sr;

        float mag = 0.0f;
        if (b1 - b0 < 1.0)
        {
            const double b = juce::jlimit (0.0, (double) maxBin - 1.0, 0.5 * (b0 + b1));
            const int idx = (int) b;
            const float frac = (float) (b - idx);
            mag = fftData[(size_t) idx] * (1.0f - frac) + fftData[(size_t) idx + 1] * frac;
        }
        else
        {
            for (int k = juce::jmax (0, (int) b0); k <= juce::jmin ((int) std::ceil (b1), maxBin); ++k)
                mag = juce::jmax (mag, fftData[(size_t) k]);
        }

        const double fc = std::sqrt (f0 * f1);
        float db = juce::Decibels::gainToDecibels (mag * norm, -120.0f)
                 + tiltDbPerOct * (float) std::log2 (fc / 1000.0);
        if (fc >= sr * 0.5)
            db = -120.0f;

        auto& v = smoothed[i];
        v += (db - v) * (db > v ? 0.6f : 0.12f);   // schneller Anstieg, langsamer Abfall
    }
}

void ResponseDisplay::timerCallback()
{
    const int mode = (int) proc.apvts.getRawParameterValue ("analyzer")->load();
    const bool wantPre  = mode == KlangbildAudioProcessor::analyzerPre  || mode == KlangbildAudioProcessor::analyzerBoth;
    const bool wantPost = mode == KlangbildAudioProcessor::analyzerPost || mode == KlangbildAudioProcessor::analyzerBoth;

    if (wantPre)  processSpectrum (preDb,  proc.preFifo.pull (fftData.data()));
    else          std::fill (preDb.begin(), preDb.end(), -120.0f);

    if (wantPost) processSpectrum (postDb, proc.postFifo.pull (fftData.data()));
    else          std::fill (postDb.begin(), postDb.end(), -120.0f);

    updateCurves();
    repaint();
}

//==============================================================================
void ResponseDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto a = plotArea();

    // Röhrenbildschirm
    g.setGradientFill (juce::ColourGradient (crt::screenCentre, bounds.getCentreX(), bounds.getCentreY(),
                                             crt::screenEdge, bounds.getX(), bounds.getY(), true));
    g.fillRect (bounds);

    // --- Raster ---------------------------------------------------------
    g.setFont (lnf.labelFontRegular (12.0f));
    const int freqLines[] = { 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000 };
    for (int f : freqLines)
    {
        const bool strong = (f == 100 || f == 1000 || f == 10000);
        g.setColour (strong ? theme::gridStrong : theme::grid);
        g.drawVerticalLine (juce::roundToInt (freqToX ((float) f)), a.getY(), a.getBottom());
    }

    const std::pair<float, const char*> freqLabels[] = { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                         { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
    g.setColour (theme::textDim);
    for (auto& [f, txt] : freqLabels)
        g.drawText (txt, juce::Rectangle<float> (40.0f, 16.0f).withCentre ({ freqToX (f), a.getBottom() + 11.0f }),
                    juce::Justification::centred);

    for (int db = -18; db <= 18; db += 6)
    {
        const float y = gainToY ((float) db);
        g.setColour (db == 0 ? theme::gridStrong.brighter (0.3f) : theme::grid);
        g.drawHorizontalLine (juce::roundToInt (y), a.getX(), a.getRight());
        g.setColour (theme::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (a.getRight() + 2.0f, y - 8.0f, 32.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (a.toNearestInt());

    // --- Analyzer -------------------------------------------------------
    auto spectrumPath = [&] (const std::vector<float>& data, bool closed)
    {
        juce::Path p;
        for (size_t i = 0; i < data.size(); ++i)
        {
            const float x = a.getX() + (float) i;
            const float y = juce::jlimit (a.getY(), a.getBottom() + 2.0f, analyzerDbToY (data[i]));
            if (i == 0) p.startNewSubPath (x, closed ? a.getBottom() + 2.0f : y);
            if (i == 0 && closed) p.lineTo (x, y);
            else if (i > 0) p.lineTo (x, y);
        }
        if (closed)
        {
            p.lineTo (a.getRight(), a.getBottom() + 2.0f);
            p.closeSubPath();
        }
        return p;
    };

    if (! preDb.empty())
    {
        g.setColour (theme::preSpectrum.withAlpha (0.45f));
        g.fillPath (spectrumPath (preDb, true));
    }
    if (! postDb.empty())
    {
        g.setGradientFill (juce::ColourGradient (theme::postSpectrum.withAlpha (0.13f), 0, a.getY(),
                                                 theme::postSpectrum.withAlpha (0.02f), 0, a.getBottom(), false));
        g.fillPath (spectrumPath (postDb, true));
        g.setColour (theme::postSpectrum.withAlpha (0.18f));
        g.strokePath (spectrumPath (postDb, false), juce::PathStrokeType (4.0f));
        g.setColour (theme::postSpectrum.withAlpha (0.85f));
        g.strokePath (spectrumPath (postDb, false), juce::PathStrokeType (1.3f));
    }

    // --- Kurve des gewählten Bands ---------------------------------------
    const float zeroY = gainToY (0.0f);
    if (selected >= 0 && ! selectedCurveDb.empty())
    {
        const bool on = proc.getBandSettings (selected).on;
        juce::Path p;
        p.startNewSubPath (a.getX(), zeroY);
        for (size_t i = 0; i < selectedCurveDb.size(); ++i)
            p.lineTo (a.getX() + (float) i, juce::jlimit (a.getY() - 2.0f, a.getBottom() + 2.0f, gainToY (selectedCurveDb[i])));
        p.lineTo (a.getRight(), zeroY);
        p.closeSubPath();
        const auto col = theme::band (selected);
        g.setColour (col.withAlpha (on ? 0.18f : 0.07f));
        g.fillPath (p);
        g.setColour (col.withAlpha (on ? 0.7f : 0.3f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // --- Summenkurve ------------------------------------------------------
    {
        juce::Path p;
        for (size_t i = 0; i < totalCurveDb.size(); ++i)
        {
            const float y = juce::jlimit (a.getY() - 2.0f, a.getBottom() + 2.0f, gainToY (totalCurveDb[i]));
            if (i == 0) p.startNewSubPath (a.getX(), y);
            else        p.lineTo (a.getX() + (float) i, y);
        }
        g.setColour (theme::curve.withAlpha (0.10f));
        g.strokePath (p, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::curve.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::curve);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- Punkte -----------------------------------------------------------
    for (int pass = 0; pass < 2; ++pass)   // gewähltes Band zuletzt zeichnen
    {
        for (int b = 0; b < eq::numBands; ++b)
        {
            if ((pass == 0) == (b == selected))
                continue;
            const auto pos = nodePosition (b);
            const bool on = proc.getBandSettings (b).on;
            const bool hi = (b == selected || b == hovered || b == dragging);
            const float r = hi ? 9.5f : 8.0f;
            const auto col = theme::band (b);
            const auto circle = juce::Rectangle<float> (r * 2, r * 2).withCentre (pos);

            if (on)
            {
                g.setColour (col.withAlpha (0.9f));
                g.fillEllipse (circle);
            }
            else
            {
                g.setColour (theme::display.withAlpha (0.85f));
                g.fillEllipse (circle);
                g.setColour (col.withAlpha (0.55f));
                g.drawEllipse (circle, 1.5f);
            }
            if (b == selected)
            {
                g.setColour (theme::curve);
                g.drawEllipse (circle.expanded (2.5f), 1.5f);
            }
            g.setColour (on ? juce::Colours::black.withAlpha (0.8f) : col.withAlpha (0.8f));
            g.setFont (lnf.labelFont (12.0f, 0.04f));
            g.drawText (juce::String (b + 1), circle, juce::Justification::centred);
        }
    }

    // --- Info-Box beim Ziehen / Überfahren ---------------------------------
    const int infoBand = dragging >= 0 ? dragging : hovered;
    if (infoBand >= 0)
    {
        auto txt = [&] (const char* n) { return proc.apvts.getParameter (KlangbildAudioProcessor::paramID (infoBand, n))->getCurrentValueAsText(); };
        const bool cut = eq::isCut (eq::typeForBand (infoBand));
        juce::String line = txt ("freq");
        if (! cut) line << "   " << txt ("gain");
        line << "   Q " << txt ("q");
        if (cut)   line << "   " << txt ("slope");

        const auto title = KlangbildAudioProcessor::bandNames()[infoBand]
                         + (proc.getBandSettings (infoBand).on ? juce::String() : juce::String (" (aus)"));
        const float w = juce::jmax (150.0f, 7.0f * (float) line.length() + 16.0f);
        auto pos = nodePosition (infoBand);
        auto box = juce::Rectangle<float> (w, 40.0f).withCentre ({ pos.x, pos.y - 38.0f });
        if (box.getY() < a.getY() + 2.0f) box.setY (pos.y + 18.0f);
        box = box.constrainedWithin (a.reduced (2.0f));

        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (theme::band (infoBand));
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setFont (lnf.labelFont (12.0f, 0.04f));
        g.drawText (title, box.withHeight (20.0f).withTrimmedTop (3.0f), juce::Justification::centred);
        g.setColour (theme::text);
        g.setFont (lnf.labelFontRegular (13.0f));
        g.drawText (line, box.withTrimmedTop (18.0f).withTrimmedBottom (3.0f), juce::Justification::centred);
    }
}

//==============================================================================
void ResponseDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h != hovered)
    {
        hovered = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ResponseDisplay::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void ResponseDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTestNode (e.position);
    if (dragging < 0)
        return;

    selected = dragging;
    if (onBandSelected)
        onBandSelected (dragging);

    for (auto* n : { "freq", "gain" })
        if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (dragging, n)))
            p->beginChangeGesture();
}

void ResponseDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || e.getDistanceFromDragStart() < 2)
        return;

    // Ein ausgeschaltetes Band wird beim Ziehen automatisch eingeschaltet
    if (! proc.getBandSettings (dragging).on)
        setParam (KlangbildAudioProcessor::paramID (dragging, "on"), 1.0f);

    const auto a = plotArea();
    const float x = juce::jlimit (a.getX(), a.getRight(), e.position.x);
    setParam (KlangbildAudioProcessor::paramID (dragging, "freq"), juce::jlimit (minFreq, maxFreq, xToFreq (x)));

    if (! eq::isCut (eq::typeForBand (dragging)))
        setParam (KlangbildAudioProcessor::paramID (dragging, "gain"),
                  juce::jlimit (-eqRangeDb, eqRangeDb, yToGain (e.position.y)));
    repaint();
}

void ResponseDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* n : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (dragging, n)))
                p->endChangeGesture();
    dragging = -1;
}

void ResponseDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int h = hitTestNode (e.position);
    if (h < 0)
        return;
    if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (h, "on")))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ResponseDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int h = hitTestNode (e.position);
    const int band = h >= 0 ? h : selected;
    if (band < 0)
        return;

    if (auto* p = proc.apvts.getParameter (KlangbildAudioProcessor::paramID (band, "q")))
    {
        const float q = proc.getBandSettings (band).q;
        const float delta = wheel.isReversed ? -wheel.deltaY : wheel.deltaY;
        const float newQ = juce::jlimit (0.1f, 18.0f, q * std::pow (2.0f, delta * 2.0f));
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (newQ));
        p->endChangeGesture();
    }
}

//==============================================================================
KlangbildAudioProcessorEditor::KlangbildAudioProcessorEditor (KlangbildAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p, lnf)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (display);
    display.onBandSelected = [this] (int b) { selectBand (b); };
    addAndMakeVisible (vuLeft);
    addAndMakeVisible (vuRight);

    const auto names = KlangbildAudioProcessor::bandNames();
    for (int b = 0; b < eq::numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        btn.setButtonText (names[b]);
        btn.setColour (juce::TextButton::buttonOnColourId, crt::band (b));
        btn.onClick = [this, b] { selectBand (b); };
        addAndMakeVisible (btn);
    }

    for (auto* s : { &freqSlider, &gainSlider, &qSlider, &outSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        s->setMouseDragSensitivity (220);
        addAndMakeVisible (*s);
    }

    onButton.setButtonText ("on");   // zeigt AN/AUS neben dem Schalter
    addAndMakeVisible (onButton);

    slopeBox.addItemList ({ "12 dB/Okt", "24 dB/Okt", "36 dB/Okt", "48 dB/Okt" }, 1);
    addAndMakeVisible (slopeBox);

    analyzerBox.addItemList ({ "Aus", "Pre", "Post", "Pre + Post" }, 1);
    addAndMakeVisible (analyzerBox);

    outAtt      = std::make_unique<SliderAttachment> (proc.apvts, "output", outSlider);
    analyzerAtt = std::make_unique<ComboAttachment>  (proc.apvts, "analyzer", analyzerBox);
    outSlider.setDoubleClickReturnValue (true, 0.0);

    rack::styleToggle (onButton);
    selectBand (2);
    setSize (rackW, rackH);
    startTimerHz (60);
}

KlangbildAudioProcessorEditor::~KlangbildAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KlangbildAudioProcessorEditor::selectBand (int band)
{
    selectedBand = band;
    display.setSelectedBand (band);
    for (int b = 0; b < eq::numBands; ++b)
        bandButtons[(size_t) b].setToggleState (b == band, juce::dontSendNotification);

    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); onAtt.reset(); slopeAtt.reset();
    auto id = [band] (const char* n) { return KlangbildAudioProcessor::paramID (band, n); };
    freqAtt = std::make_unique<SliderAttachment> (proc.apvts, id ("freq"), freqSlider);
    gainAtt = std::make_unique<SliderAttachment> (proc.apvts, id ("gain"), gainSlider);
    qAtt    = std::make_unique<SliderAttachment> (proc.apvts, id ("q"),    qSlider);
    onAtt   = std::make_unique<ButtonAttachment> (proc.apvts, id ("on"),   onButton);

    for (auto [s, n] : { std::pair { &freqSlider, "freq" }, std::pair { &gainSlider, "gain" }, std::pair { &qSlider, "q" } })
        if (auto* rp = proc.apvts.getParameter (id (n)))
            s->setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));

    onButton.getProperties().set ("lamp", (int) crt::band (band).getARGB());

    const bool cut = eq::isCut (eq::typeForBand (band));
    gainSlider.setEnabled (! cut);
    gainSlider.setAlpha (cut ? 0.45f : 1.0f);
    slopeBox.setVisible (cut);
    if (cut)
        slopeAtt = std::make_unique<ComboAttachment> (proc.apvts, id ("slope"), slopeBox);
    repaint();
}

void KlangbildAudioProcessorEditor::timerCallback()
{
    constexpr float dt = 1.0f / 60.0f;
    vuLeft.update (proc.vuL.load(), proc.meterL.exchange (0.0f), dt);
    vuRight.update (proc.vuR.load(), proc.meterR.exchange (0.0f), dt);

    for (int b = 0; b < eq::numBands; ++b)
    {
        auto& btn = bandButtons[(size_t) b];
        const bool on = proc.getBandSettings (b).on;
        if ((bool) btn.getProperties()["bandOn"] != on)
        {
            btn.getProperties().set ("bandOn", on);
            btn.repaint();
        }
    }
}

//==============================================================================
void KlangbildAudioProcessorEditor::renderPanel()
{
    panelImage = rack::renderPanel (rackW, rackH, 13);
    juce::Graphics g (panelImage);
    const juce::String titles[] = { "FREQUENZGANG", "BANDWAHL", "BAND" };
    for (size_t i = 0; i < sections.size(); ++i)
        rack::drawSection (g, sections[i], titles[i], lnf);

    rack::drawBezel (g, bezel);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (display.getBounds().toFloat().expanded (3.0f), 16.0f);

    rack::drawBezel (g, vuBezelL);
    rack::drawBezel (g, vuBezelR);

    for (auto [sl, n] : { std::pair { &freqSlider, "FREQUENZ" }, std::pair { &gainSlider, "GAIN" },
                          std::pair { &qSlider, "G\xc3\x9cTE (Q)" }, std::pair { &outSlider, "PEGEL (DB)" } })
        rack::drawKnobLabel (g, *sl, rack::u8 (n), lnf, 12.0f);

    rack::drawLabel (g, rack::u8 ("PUNKT ZIEHEN: FREQUENZ/GAIN  \xc2\xb7  MAUSRAD: G\xc3\x9cTE  \xc2\xb7  DOPPELKLICK: AN/AUS"),
                     hintArea.toFloat(), lnf, 9.5f, juce::Justification::centred);
    rack::drawNameplate (g, nameplate, "EQ", "8-BAND-EQUALIZER", lnf);
    rack::drawLabel (g, "ANALYZER", analyzerBox.getBounds().toFloat().withY ((float) analyzerBox.getY() - 16.0f).withHeight (14.0f), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawLabel (g, "AUSGANG", vuBezelL.toFloat().withY ((float) vuBezelL.getY() - 16.0f).withHeight (14.0f), lnf, 11.0f, juce::Justification::centredLeft);
    rack::drawTypeTag (g, rack::u8 ("TYP KB-800  \xc2\xb7  NR. 0601"), tagArea, lnf);
}

void KlangbildAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.drawImageAt (panelImage, 0, 0);
    // wechselt mit dem gewählten Band
    rack::drawLabel (g, KlangbildAudioProcessor::bandNames()[selectedBand].toUpperCase(), onLabelArea.toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
    if (slopeBox.isVisible())
        rack::drawLabel (g, "FLANKE", slopeLabelArea.toFloat(), lnf, 11.0f, juce::Justification::centredLeft);
}

void KlangbildAudioProcessorEditor::resized()
{
    const int top = 30, bottom = rackH - 34;
    const int r = top + 372;
    sections[0] = { 72, top, 958, r - top - 12 };
    sections[1] = { 72, r, 400, bottom - r };
    sections[2] = { 484, r, 546, bottom - r };

    bezel = sections[0].reduced (16).withTrimmedTop (10);
    display.setBounds (bezel.reduced (14));

    // Bandwahl
    const auto bs = sections[1];
    const int bw = 86, bh = 34, gap = 6, bx = bs.getX() + (bs.getWidth() - (4 * bw + 3 * gap)) / 2;
    for (int b = 0; b < eq::numBands; ++b)
    {
        const int row = b / 4, col = b % 4;
        bandButtons[(size_t) b].setBounds (bx + col * (bw + gap), bs.getY() + 34 + row * (bh + 10), bw, bh);
    }
    hintArea = { bs.getX() + 10, bs.getY() + 34 + 2 * (bh + 10) + 18, bs.getWidth() - 20, 14 };

    // Band: Frequenz, Gain, Güte | an/aus, Flanke
    const auto ks = sections[2];
    constexpr int kw = 128;
    freqSlider.setBounds (ks.getX() + 10,           ks.getY() + 26, kw, kw);
    gainSlider.setBounds (ks.getX() + 10 + kw,      ks.getY() + 26, kw, kw);
    qSlider.setBounds    (ks.getX() + 10 + 2 * kw,  ks.getY() + 26, kw, kw);
    const int cx = ks.getX() + 10 + 3 * kw + 12, cw = ks.getRight() - 12 - cx;
    onLabelArea = { cx, ks.getY() + 30, cw, 14 };
    onButton.setBounds (cx, ks.getY() + 48, cw, 40);
    slopeLabelArea = { cx, ks.getY() + 104, cw, 14 };
    slopeBox.setBounds (cx, ks.getY() + 122, cw, 28);

    // rechte Spalte
    const int colX = 1046, colW = rackW - 56 - 16 - colX;
    nameplate = { colX, top - 8, colW, 88 };
    analyzerBox.setBounds (colX, top + 112, colW, 28);
    vuBezelL = { colX, top + 172, colW, 112 };
    vuBezelR = { colX, top + 292, colW, 112 };
    vuLeft.setBounds (vuBezelL.reduced (7));
    vuRight.setBounds (vuBezelR.reduced (7));
    outSlider.setBounds (juce::Rectangle<int> (128, 128).withCentre ({ colX + colW / 2, top + 482 }));
    tagArea = { colX - 60, rackH - 28, colW + 60, 16 };
    renderPanel();
}
