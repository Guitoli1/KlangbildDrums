#pragma once

// VCA-Buskompressor nach dem Vorbild klassischer Mischpult-Buskompressoren:
//  - Spitzenwert-Erkennung, beide Kanäle gemeinsam geregelt (Stereobild bleibt stehen)
//  - feste Stufen für Ratio, Attack und Release, dazu "Auto"-Release (programmabhängig)
//  - Hochpass im Regelweg, damit Bass und Kick nicht ständig pumpen

#include <cmath>
#include <algorithm>
#include "EQDsp.h"

namespace bus
{
constexpr float ratios[]   = { 2.0f, 4.0f, 10.0f };
constexpr float attacksMs[] = { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
constexpr float releasesS[] = { 0.1f, 0.3f, 0.6f, 1.2f, -1.0f };   // -1 = Auto
constexpr float scFilterHz[] = { 0.0f, 60.0f, 90.0f, 150.0f };       // 0 = aus

class Compressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        aDet = coef (0.001);
        reset();
        setParams (-10.0f, 1, 4, 4, 0);
    }

    void reset() { det = gr = grFast = grSlow = 0.0; hpL.reset(); hpR.reset(); }

    void setParams (float thresholdDb, int ratioIndex, int attackIndex, int releaseIndex, int filterIndex)
    {
        threshold = thresholdDb;
        ratio = ratios[std::clamp (ratioIndex, 0, 2)];
        aAtt = coef (attacksMs[std::clamp (attackIndex, 0, 5)] * 0.001);
        const float rel = releasesS[std::clamp (releaseIndex, 0, 4)];
        autoRelease = rel < 0.0f;
        aRel = coef (autoRelease ? 0.1 : rel);
        aSlowCharge = coef (0.4);
        aSlowRel = coef (1.2);

        const int f = std::clamp (filterIndex, 0, 3);
        if (f != filterIndexNow)
        {
            filterIndexNow = f;
            useFilter = f > 0;
            if (useFilter)
                hp = eq::makeHighPass (fs, scFilterHz[f], 0.7071);
        }
    }

    // Liefert die Pegelreduktion in dB (>= 0)
    double process (float l, float r) noexcept
    {
        if (useFilter) { l = hpL.process (hp, l); r = hpR.process (hp, r); }
        const double s = std::max (std::abs ((double) l), std::abs ((double) r));
        det = std::max (s, det * aDet);
        const double tgt = staticGr (20.0 * std::log10 (det + 1.0e-9));

        if (! autoRelease)
        {
            gr = tgt + (gr - tgt) * (tgt > gr ? aAtt : aRel);
            return gr;
        }
        // Auto: schnelle Erholung nach kurzen Spitzen, langsame nach anhaltender Kompression
        grFast = tgt + (grFast - tgt) * (tgt > grFast ? aAtt : aRel);
        grSlow = grFast + (grSlow - grFast) * (grFast > grSlow ? aSlowCharge : aSlowRel);
        gr = std::max (grFast, 0.7 * grSlow);
        return gr;
    }

    double staticGr (double lvl) const noexcept
    {
        constexpr double knee = 4.0;
        const double over = lvl - threshold;
        if (over <= -knee * 0.5) return 0.0;
        if (over >= knee * 0.5)  return over * (1.0 - 1.0 / ratio);
        const double k = over + knee * 0.5;
        return (1.0 - 1.0 / ratio) * k * k / (2.0 * knee);
    }

private:
    double coef (double seconds) const { return std::exp (-1.0 / (seconds * fs)); }

    double fs = 48000.0;
    double aDet = 0, aAtt = 0, aRel = 0, aSlowCharge = 0, aSlowRel = 0;
    double det = 0, gr = 0, grFast = 0, grSlow = 0;
    double threshold = -10.0, ratio = 4.0;
    bool autoRelease = true, useFilter = false;
    int filterIndexNow = -1;
    eq::Coeffs hp;
    eq::BiquadState hpL, hpR;
};
} // namespace bus
