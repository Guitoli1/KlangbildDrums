#pragma once

// Plattenhall nach J. Dattorro, "Effect Design Part 1: Reverberator and Other Filters" (JAES 1997).
// Der Algorithmus wurde eigens nach dem Klang einer Hallplatte entworfen:
// Eingangsdiffusion (4 Allpässe) -> zwei über Kreuz gekoppelte "Tank"-Hälften mit modulierten
// Allpässen, Verzögerungen und Dämpfung -> Stereo-Abgriffe an mehreren Stellen im Tank.
// Alle Längen beziehen sich im Original auf 29 761 Hz und werden auf die Projekt-Samplerate umgerechnet.

#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace plate
{
constexpr double refRate = 29761.0;

class Delay
{
public:
    void prepare (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // Wert von vor d Samples (d >= 1)
    float tap (int d) const noexcept { return buf[(size_t) ((w - d) & mask)]; }
    float tapFrac (float d) const noexcept
    {
        const int i = (int) d;
        const float f = d - (float) i;
        return tap (i) + f * (tap (i + 1) - tap (i));
    }
private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

// Allpass in Schroeder-Form; die interne Verzögerungsleitung ist für Abgriffe zugänglich
struct Allpass
{
    Delay line;
    int length = 1;
    void prepare (int len, int extra = 0) { length = std::max (1, len); line.prepare (length + extra + 2); }
    float process (float x, float g) noexcept { return processAt (x, g, (float) length); }
    float processAt (float x, float g, float d) noexcept
    {
        const float delayed = line.tapFrac (d);
        const float v = x + g * delayed;
        line.push (v);
        return delayed - g * v;
    }
};

class Reverb
{
public:
    void prepare (double sampleRate, double maxPreDelaySeconds)
    {
        fs = sampleRate;
        k = fs / refRate;
        auto s = [this] (double n) { return std::max (1, (int) std::lround (n * k)); };

        pre.prepare ((int) (maxPreDelaySeconds * fs) + 8);
        in1.prepare (s (142)); in2.prepare (s (107)); in3.prepare (s (379)); in4.prepare (s (277));

        excursion = (float) (16.0 * k);
        apL1.prepare (s (672), (int) excursion + 4);
        apR1.prepare (s (908), (int) excursion + 4);
        dL1.prepare (s (4453)); dL1Len = s (4453);
        apL2.prepare (s (1800));
        dL2.prepare (s (3720)); dL2Len = s (3720);
        dR1.prepare (s (4217)); dR1Len = s (4217);
        apR2.prepare (s (2656));
        dR2.prepare (s (3163)); dR2Len = s (3163);

        // Ausgangsabgriffe (Tabelle aus dem Aufsatz)
        tapsL = { s (266), s (2974), s (1913), s (1996), s (1990), s (187), s (1066) };
        tapsR = { s (353), s (3627), s (1228), s (2673), s (2111), s (335), s (121) };

        loopSeconds = (672 + 4453 + 1800 + 3720 + 908 + 4217 + 2656 + 3163) / refRate;
        bandwidthLp = coefLp (fs * 0.45);
        reset();
        setParams (2.0f, 0.4f, 20.0f, 0.3f);
    }

    void reset()
    {
        for (auto* d : { &pre, &dL1, &dL2, &dR1, &dR2 }) d->reset();
        for (auto* a : { &in1, &in2, &in3, &in4, &apL1, &apR1, &apL2, &apR2 }) a->line.reset();
        bwState = dampL = dampR = fbL = fbR = 0.0f;
        lfoPhase = 0.0;
    }

    // decaySeconds: Nachhallzeit (T60), damping 0..1 (mehr = dunkler), preDelayMs, modulation 0..1
    void setParams (float decaySeconds, float damping, float preDelayMs, float modulation)
    {
        // Pro Umlauf durch beide Tank-Hälften wird viermal mit "decay" multipliziert
        // leichte Korrektur: lange Zeiten klingen im Tank sonst etwas zu kurz aus
        const double t60 = std::max (0.1f, decaySeconds) * (1.0 + 0.03 * std::clamp ((double) decaySeconds - 1.0, 0.0, 3.0));
        decay = (float) std::pow (10.0, -3.0 * loopSeconds / (4.0 * t60));
        decay = std::min (decay, 0.9995f);
        decayDiffusion2 = std::clamp (decay + 0.15f, 0.25f, 0.5f);
        // Dämpfung als Tiefpass im Tank: 0 = sehr hell (18 kHz), 1 = dunkel (1,5 kHz)
        dampCoef = coefLp (18000.0 * std::pow (1500.0 / 18000.0, (double) damping));
        preDelay = std::max (1, (int) (preDelayMs * 0.001 * fs));
        modDepth = modulation;
        lfoInc = 0.9 / fs;   // ca. 0,9 Hz wie im Aufsatz empfohlen
    }

    void process (float in, float& outL, float& outR) noexcept
    {
        // Vorverzögerung und Bandbreite
        const float x = pre.tap (preDelay);
        pre.push (in);
        bwState += (x - bwState) * (1.0f - bandwidthLp);
        float v = bwState;

        // Eingangsdiffusion
        v = in1.process (v, 0.75f);
        v = in2.process (v, 0.75f);
        v = in3.process (v, 0.625f);
        v = in4.process (v, 0.625f);

        // langsam modulierte Tank-Allpässe (verhindert metallisches Klingeln)
        lfoPhase += lfoInc;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        const float lfo = (float) std::sin (6.283185307179586 * lfoPhase);
        const float mL = (float) apL1.length + excursion * modDepth * lfo;
        const float mR = (float) apR1.length - excursion * modDepth * lfo;

        // linke Tank-Hälfte
        float l = apL1.processAt (v + fbR, -0.70f, mL);
        dL1.push (l);
        l = dL1.tap (dL1Len);
        dampL += (l - dampL) * (1.0f - dampCoef);
        l = apL2.process (dampL * decay, decayDiffusion2);
        dL2.push (l);
        const float toRight = dL2.tap (dL2Len) * decay;

        // rechte Tank-Hälfte
        float r = apR1.processAt (v + fbL, -0.70f, mR);
        dR1.push (r);
        r = dR1.tap (dR1Len);
        dampR += (r - dampR) * (1.0f - dampCoef);
        r = apR2.process (dampR * decay, decayDiffusion2);
        dR2.push (r);
        const float toLeft = dR2.tap (dR2Len) * decay;

        fbL = toRight;   // Ausgang links speist die rechte Hälfte und umgekehrt
        fbR = toLeft;

        outL = 0.6f * (dR1.tap (tapsL[0]) + dR1.tap (tapsL[1]) - apR2.line.tap (tapsL[2]) + dR2.tap (tapsL[3])
                       - dL1.tap (tapsL[4]) - apL2.line.tap (tapsL[5]) - dL2.tap (tapsL[6]));
        outR = 0.6f * (dL1.tap (tapsR[0]) + dL1.tap (tapsR[1]) - apL2.line.tap (tapsR[2]) + dL2.tap (tapsR[3])
                       - dR1.tap (tapsR[4]) - apR2.line.tap (tapsR[5]) - dR2.tap (tapsR[6]));
    }

private:
    float coefLp (double fc) const { return (float) std::exp (-6.283185307179586 * fc / fs); }

    double fs = 48000.0, k = 1.0, loopSeconds = 0.7, lfoPhase = 0.0, lfoInc = 0.0;
    Delay pre, dL1, dL2, dR1, dR2;
    Allpass in1, in2, in3, in4, apL1, apR1, apL2, apR2;
    int dL1Len = 1, dL2Len = 1, dR1Len = 1, dR2Len = 1, preDelay = 1;
    std::array<int, 7> tapsL {}, tapsR {};
    float excursion = 16.0f, modDepth = 0.3f;
    float decay = 0.5f, decayDiffusion2 = 0.5f, dampCoef = 0.0f, bandwidthLp = 0.0f;
    float bwState = 0, dampL = 0, dampR = 0, fbL = 0, fbR = 0;
};
} // namespace plate
