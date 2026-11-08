#pragma once

// Klangbild Band – Bandmaschinen-Modell
// Kette je Kanal: Eingang -> Aufnahme-Entzerrung (Höhen anheben) -> Band-Sättigung (ADAA)
//                 -> Wiedergabe-Entzerrung (Höhen absenken) -> Kopfresonanz (Head Bump) -> Spaltverluste (Tiefpass)
//                 -> Gleichlaufschwankungen (Wow & Flutter, modulierte Verzögerung) -> Bandrauschen

#include <cmath>
#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>
#include "EQDsp.h"

namespace tape
{

// Weiche Sättigung f(x) = x / sqrt(1 + x²) mit Stammfunktion F(x) = sqrt(1 + x²)
// -> Antiderivative Anti-Aliasing (1. Ordnung), damit keine Spiegelfrequenzen entstehen
struct Saturator
{
    double x1 = 0.0;
    void reset() { x1 = 0.0; }

    static double f (double x)  { return x / std::sqrt (1.0 + x * x); }
    static double F (double x)  { return std::sqrt (1.0 + x * x); }

    double process (double x)
    {
        const double d = x - x1;
        const double y = std::abs (d) > 1.0e-5 ? (F (x) - F (x1)) / d : f (0.5 * (x + x1));
        x1 = x;
        return y;
    }
};

struct SpeedProfile
{
    double emphasisDb;      // Aufnahme-/Wiedergabe-Entzerrung (Höhen-Kompression bei hohem Pegel)
    double bumpHz, bumpDb;  // Kopfresonanz
    double lowLossHz;       // Tiefen-Verlust unterhalb der Resonanz
    double hfHz;            // Höhen-Grenze
    double hissDb;          // Rauschabstand-Unterschied
};

inline SpeedProfile profileFor (int speedIndex)
{
    switch (speedIndex)
    {
        case 0:  return { 8.0, 48.0, 2.6, 22.0, 12500.0, 3.0 };    // 19 cm/s (7,5 ips): warm, rund, früh gesättigt
        case 2:  return { 3.0, 110.0, 1.4, 35.0, 30000.0, -3.0 };  // 76 cm/s (30 ips): offen, straffer Bass
        default: return { 5.0, 72.0, 2.0, 28.0, 21000.0, 0.0 };    // 38 cm/s (15 ips): der Studio-Klassiker
    }
}

class Machine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        baseDelay = 0.0025 * sr;
        size_t n = 1; while (n < (size_t) (baseDelay * 2 + 16)) n <<= 1;
        for (auto& d : delay) d.assign (n, 0.0f);
        mask = n - 1;
        lfoInc[0] = 0.55 / sr; lfoInc[1] = 6.8 / sr; lfoInc[2] = 11.3 / sr;
        driftCoef = 1.0 - std::exp (-2.0 * 3.14159265358979 * 0.8 / sr);
        hissLP = std::exp (-2.0 * 3.14159265358979 * 9000.0 / sr);
        reset();
        lastSpeed = -1;
    }

    void reset()
    {
        for (auto& s : sat) s.reset();
        for (auto* st : { &preL, &preR, &postL, &postR, &bumpL, &bumpR, &lowL, &lowR, &hfL, &hfR, &hf2L, &hf2R })
            st->reset();
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        writePos = 0;
        lfo = { 0.0, 0.31, 0.77 };
        drift = driftTarget = 0.0; driftTimer = 0;
        hissStateL = hissStateR = 0.0f;
    }

    double latencySamples() const { return baseDelay + 0.5; }   // +0,5: ADAA

    // speedIndex 0..2, bias -1..+1 (unter-/übersteuert vormagnetisiert), wow/flutter/hiss 0..1
    void setParams (int speedIndex, float biasAmount, float wowAmount, float flutterAmount, float hissAmount)
    {
        bias = biasAmount; wow = wowAmount; flutter = flutterAmount;
        const auto p = profileFor (speedIndex);
        const double hfShift = std::pow (2.0, 0.35 * biasAmount * -1.0);   // Unter-Vormagnetisierung = heller, Über- = dunkler
        if (speedIndex != lastSpeed || ! exactlyEqual (biasAmount, lastBias))
        {
            lastSpeed = speedIndex; lastBias = biasAmount;
            const double nyq = 0.45 * sr;
            pre  = eq::makeHighShelf (sr, 3000.0, 0.7, p.emphasisDb);
            post = eq::makeHighShelf (sr, 3000.0, 0.7, -p.emphasisDb);
            bump = eq::makePeak (sr, p.bumpHz, 1.1, p.bumpDb);
            low  = eq::makeHighPass (sr, p.lowLossHz, 0.9);
            const double hf = std::min (nyq, p.hfHz * hfShift);
            // bei hoher Samplerate bzw. Geschwindigkeit liegt die Grenze über dem Hörbereich -> Filter neutral
            hfA = hf >= nyq ? eq::Coeffs{} : eq::makeLowPass (sr, hf, 0.707);
            hfB = (speedIndex == 0 && hf * 1.5 < nyq) ? eq::makeLowPass (sr, hf * 1.5, 0.6) : eq::Coeffs{};
            hissGain = (float) (std::pow (10.0, (-48.0 + p.hissDb) / 20.0));
        }
        // Bias: unter-vormagnetisiert -> mehr Verzerrung und Asymmetrie, über- -> sauberer
        drive = 2.0 * std::pow (2.0, -0.6 * biasAmount);
        asym  = 0.035 + 0.08 * std::max (0.0f, -biasAmount);
        hiss  = hissAmount * hissAmount;
    }

    void process (float& l, float& r)
    {
        // 1) Aufnahme-Entzerrung + Sättigung
        double xl = preL.process (pre, l), xr = preR.process (pre, r);
        xl = saturate (0, xl); xr = saturate (1, xr);
        float yl = postL.process (post, (float) xl), yr = postR.process (post, (float) xr);

        // 2) Wiedergabekopf: Head Bump, Tiefen- und Höhenverluste
        yl = hfL.process (hfA, hf2L.process (hfB, lowL.process (low, bumpL.process (bump, yl))));
        yr = hfR.process (hfA, hf2R.process (hfB, lowR.process (low, bumpR.process (bump, yr))));

        // 3) Gleichlauf: beide Spuren laufen auf demselben Band -> gemeinsame Modulation
        const double tau = 6.283185307179586;
        for (int i = 0; i < 3; ++i) { lfo[(size_t) i] += lfoInc[(size_t) i]; if (lfo[(size_t) i] >= 1.0) lfo[(size_t) i] -= 1.0; }
        if (--driftTimer <= 0) { driftTimer = (int) (sr * 0.4); driftTarget = rnd() * 2.0 - 1.0; }
        drift += (driftTarget - drift) * driftCoef;
        const double wowSec = 0.0008 * wow * (0.7 * std::sin (tau * lfo[0]) + 0.5 * drift);
        const double flutSec = flutter * (0.000030 * std::sin (tau * lfo[1]) + 0.000012 * std::sin (tau * lfo[2]));
        const double d = baseDelay + (wowSec + flutSec) * sr;

        delay[0][writePos] = yl; delay[1][writePos] = yr;
        yl = readHermite (0, d); yr = readHermite (1, d);
        writePos = (writePos + 1) & mask;

        // 4) Bandrauschen (leicht höhenbetont, unkorreliert)
        if (hiss > 0.0f)
        {
            const float nl = (float) (rnd() * 2.0 - 1.0), nr = (float) (rnd() * 2.0 - 1.0);
            hissStateL = nl + (float) hissLP * (hissStateL - nl);
            hissStateR = nr + (float) hissLP * (hissStateR - nr);
            const float g = hissGain * hiss * 1.7f;
            yl += (nl - 0.6f * hissStateL) * g;
            yr += (nr - 0.6f * hissStateR) * g;
        }
        l = yl; r = yr;
    }

private:
    static bool exactlyEqual (float a, float b) { return ! (a < b) && ! (b < a); }

    double saturate (int ch, double x)
    {
        // Pegel: 0 VU (−18 dBFS) liegt im linearen Bereich, ab etwa −6 dBFS wird es deutlich
        const double b = asym;
        const double y = sat[(size_t) ch].process (x * drive + b) - Saturator::f (b);
        return y / (drive * 1.0);
    }

    float readHermite (int ch, double d) const
    {
        const double rp = (double) writePos - d;
        const auto i = (int64_t) std::floor (rp);
        const float t = (float) (rp - (double) i);
        auto at = [&] (int64_t k) { return delay[(size_t) ch][(size_t) (k & (int64_t) mask)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    double rnd() { seed = seed * 1664525u + 1013904223u; return (double) (seed >> 8) / 16777216.0; }

    double sr = 48000.0, baseDelay = 120.0;
    std::array<Saturator, 2> sat;
    eq::Coeffs pre, post, bump, low, hfA, hfB;
    eq::BiquadState preL, preR, postL, postR, bumpL, bumpR, lowL, lowR, hfL, hfR, hf2L, hf2R;
    std::array<std::vector<float>, 2> delay;
    size_t mask = 0, writePos = 0;
    std::array<double, 3> lfo { 0, 0, 0 }, lfoInc { 0, 0, 0 };
    double drift = 0, driftTarget = 0, driftCoef = 0, hissLP = 0;
    int driftTimer = 0;
    uint32_t seed = 12345u;
    float bias = 0, wow = 0, flutter = 0, hiss = 0, hissGain = 0, hissStateL = 0, hissStateR = 0;
    double drive = 2.0, asym = 0.08;
    int lastSpeed = -1; float lastBias = 99.0f;
};

} // namespace tape
