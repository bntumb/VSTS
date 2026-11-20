#pragma once
// Small JUCE-free DSP building blocks: RBJ high-shelf biquad and a Dattorro-style plate reverb.

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>

// ---------------------------------------------------------------- high-shelf "air" filter
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setHighShelf (float sampleRate, float freq, float gainDb, float slope = 0.8f)
    {
        const double A    = std::pow (10.0, (double) gainDb / 40.0);
        const double w0   = 6.283185307179586 * (double) freq / (double) sampleRate;
        const double cs   = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / (double) slope - 1.0) + 2.0);
        const double tsa  = 2.0 * std::sqrt (A) * alpha;

        const double B0 =        A * ((A + 1) + (A - 1) * cs + tsa);
        const double B1 = -2.0 * A * ((A - 1) + (A + 1) * cs);
        const double B2 =        A * ((A + 1) + (A - 1) * cs - tsa);
        const double A0 =             (A + 1) - (A - 1) * cs + tsa;
        const double A1 =  2.0 *     ((A - 1) - (A + 1) * cs);
        const double A2 =             (A + 1) - (A - 1) * cs - tsa;

        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = z2 = 0.0f; }
};

// ---------------------------------------------------------------- plate reverb
// Structure after Dattorro, "Effect Design Part 1" (1997): pre-delay, input band-limiting,
// four input diffusers, then a figure-of-eight tank with modulated all-passes.
// Brighter and cleaner than a Freeverb: low cut on the input keeps it tight and
// the damping is set high so the tail stays airy.
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const float s = sr / 29761.0f;
        auto L = [s] (int n) { return std::max (1, (int) std::lround ((float) n * s)); };

        pre.init ((int) (0.05f * sr) + 4);
        preDelay = (int) (0.020f * sr);

        const int   inD[4] = { 142, 107, 379, 277 };
        const float inG[4] = { 0.75f, 0.75f, 0.625f, 0.625f };
        for (int i = 0; i < 4; ++i) { inAp[i].init (L (inD[i]), inG[i]); }

        exc = 8.0f * s;
        apL1.init (L (672) + (int) exc + 4, 0.7f);  d1L.init (L (4453));
        apL2.init (L (1800), 0.5f);                 d2L.init (L (3720));
        apR1.init (L (908) + (int) exc + 4, 0.7f);  d1R.init (L (4217));
        apR2.init (L (2656), 0.5f);                 d2R.init (L (3163));
        nL1 = (float) L (672); nR1 = (float) L (908);

        const int tl[14] = { 266, 2974, 1913, 1996, 1990, 187, 1066,
                             353, 3627, 1228, 2673, 2111, 335, 121 };
        for (int i = 0; i < 14; ++i) tap[i] = L (tl[i]);

        hpCoef  = 1.0f - std::exp (-6.2831853f * 150.0f / sr);
        bwCoef  = 1.0f - std::exp (-6.2831853f * 12000.0f / sr);
        dampCoef = 1.0f - std::exp (-6.2831853f * 7500.0f / sr);
        lfoInc1 = 6.2831853f * 0.53f / sr;
        lfoInc2 = 6.2831853f * 0.67f / sr;
        reset();
    }

    void reset()
    {
        pre.clear();
        for (auto& a : inAp) a.clear();
        for (Line* l : std::initializer_list<Line*> { &apL1.line, &apL2.line, &apR1.line, &apR2.line,
                                                      &d1L, &d2L, &d1R, &d2R }) l->clear();
        hpState = bwState = dampL = dampR = fbL = fbR = 0.0f;
        lfo1 = 0.0f; lfo2 = 1.7f;
    }

    // size 0..1 -> reverb time 0.8 s .. 6 s. The tank loop is ~0.725 s long and the signal passes
    // through `decay` twice per loop, so decay = 10^(-1.5 * loopTime / RT60).
    void setSize (float size01)
    {
        const float rt60 = 0.8f * std::pow (7.5f, std::clamp (size01, 0.0f, 1.0f));
        decay = std::min (0.95f, std::pow (10.0f, -1.5f * 0.7254f / rt60));
    }

    void process (float in, float& outL, float& outR)
    {
        pre.write (in);
        float x = pre.read (preDelay);

        hpState += hpCoef * (x - hpState);    x -= hpState;             // low cut
        bwState += bwCoef * (x - bwState);    x  = bwState;             // bandwidth

        for (auto& a : inAp) x = a.process (x);

        lfo1 += lfoInc1; if (lfo1 > 6.2831853f) lfo1 -= 6.2831853f;
        lfo2 += lfoInc2; if (lfo2 > 6.2831853f) lfo2 -= 6.2831853f;

        // left branch
        float a = apL1.processMod (x + fbR, nL1 + exc * std::sin (lfo1));
        a = d1L.process (a);
        dampL += dampCoef * (a - dampL);
        a = apL2.process (dampL * decay);
        const float newFbL = d2L.process (a);

        // right branch
        float b = apR1.processMod (x + fbL, nR1 + exc * std::sin (lfo2));
        b = d1R.process (b);
        dampR += dampCoef * (b - dampR);
        b = apR2.process (dampR * decay);
        const float newFbR = d2R.process (b);

        fbL = newFbL; fbR = newFbR;

        outL = 0.4f * (d1R.tap (tap[0]) + d1R.tap (tap[1]) - apR2.line.tap (tap[2]) + d2R.tap (tap[3])
                       - d1L.tap (tap[4]) - apL2.line.tap (tap[5]) - d2L.tap (tap[6]));
        outR = 0.4f * (d1L.tap (tap[7]) + d1L.tap (tap[8]) - apL2.line.tap (tap[9]) + d2L.tap (tap[10])
                       - d1R.tap (tap[11]) - apR2.line.tap (tap[12]) - d2R.tap (tap[13]));
    }

private:
    struct Line
    {
        std::vector<float> buf;
        int w = 0, n = 1;
        void init (int len) { n = std::max (2, len + 1); buf.assign ((size_t) n, 0.0f); w = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void write (float x) { buf[(size_t) w] = x; if (++w >= n) w = 0; }
        // sample written `d` steps ago (d = 1 is the most recent write)
        float tap (int d) const { int i = w - d; if (i < 0) i += n; return buf[(size_t) i]; }
        float read (int d) const { return tap (d); }
    };

    struct Allpass
    {
        Line line; int N = 1; float g = 0.5f;
        void init (int len, float gain) { N = len; g = gain; line.init (len + 2); }
        void clear() { line.clear(); }
        float process (float x)
        {
            const float d = line.tap (N);
            const float v = x - g * d;
            line.write (v);
            return d + g * v;
        }
        float processMod (float x, float delay)       // fractional delay, linear interpolation
        {
            const int   i = (int) delay;
            const float f = delay - (float) i;
            const float d = line.tap (i) * (1.0f - f) + line.tap (i + 1) * f;
            const float v = x - g * d;
            line.write (v);
            return d + g * v;
        }
    };

    // delay line with a fixed length (set via init)
    struct Delay : Line
    {
        int len = 1;
        void init (int l) { len = l; Line::init (l); }
        float process (float x) { const float y = Line::tap (len); Line::write (x); return y; }
    };

    float sr = 48000.0f, decay = 0.85f, exc = 8.0f, nL1 = 700.0f, nR1 = 900.0f;
    float hpCoef = 0, bwCoef = 0, dampCoef = 0, lfoInc1 = 0, lfoInc2 = 0, lfo1 = 0, lfo2 = 0;
    float hpState = 0, bwState = 0, dampL = 0, dampR = 0, fbL = 0, fbR = 0;
    int   preDelay = 960, tap[14] = {};

    Line pre;
    Allpass inAp[4];
    Allpass apL1, apL2, apR1, apR2;
    Delay   d1L, d2L, d1R, d2R;
};
