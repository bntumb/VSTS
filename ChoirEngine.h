#pragma once
// ChoirEngine: turns one mono voice into an ensemble of N slightly different singers.
// Pure C++ (no JUCE dependency) so it can be unit-tested offline.
//
// Each virtual singer has its own:
//   - static detune (cents)           - entry timing offset (ms)
//   - vibrato rate/phase              - slow pitch drift
//   - pan and level                   - tone (low-pass brightness)
// Pitch is shifted with the classic two-head crossfaded delay-ramp shifter, so
// detune and vibrato are real pitch changes rather than just chorus modulation.

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <vector>

class ChoirEngine
{
public:
    static constexpr int maxVoices = 32;

    struct Params
    {
        int   numVoices    = 16;
        float detuneCents  = 15.0f;  // spread of static detune per voice (+/-)
        float timingMs     = 35.0f;  // max entry delay spread
        float vibratoCents = 10.0f;  // vibrato depth
        float vibratoRate  = 5.0f;   // Hz (each voice varies +/-15%)
        float width        = 0.8f;   // stereo spread 0..1
    };

    void prepare (double sampleRate, unsigned seed = 1234)
    {
        sr         = (float) sampleRate;
        invSr      = 1.0f / sr;
        windowSize = 0.05f * sr;                    // 50 ms pitch-shift window
        const int bufSize = (int) std::ceil (0.4 * sampleRate);

        std::mt19937 rng (seed);
        auto rnd = [&rng] { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); };

        for (auto& v : voices)
            v.init (bufSize, rnd);

        const float coef = 1.0f - std::exp (-1.0f / (0.08f * sr));   // ~80 ms smoothing
        for (auto* s : { &detune, &timing, &vib, &norm })
            s->coef = coef;

        detune.snap (15.0f);  timing.snap (0.035f * sr);  vib.snap (10.0f);
        norm.snap (1.3f / std::sqrt (16.0f));
        prepared = true;
    }

    // in: mono input. outL/outR: wet choir output (overwritten).
    void process (const float* in, float* outL, float* outR, int n, const Params& p)
    {
        if (! prepared)
        {
            std::fill (outL, outL + n, 0.0f);
            std::fill (outR, outR + n, 0.0f);
            return;
        }

        const int nv = std::clamp (p.numVoices, 1, maxVoices);
        detune.tgt = p.detuneCents;
        timing.tgt = std::clamp (p.timingMs, 0.0f, 100.0f) * 0.001f * sr;
        vib.tgt    = p.vibratoCents;
        norm.tgt   = 1.3f / std::sqrt ((float) nv);   // 1.3 = makeup for pan law / decorrelation

        constexpr float pi = 3.14159265358979f;
        std::array<float, maxVoices> panL {}, panR {};
        for (int v = 0; v < maxVoices; ++v)
        {
            const float pan   = std::clamp (voices[(size_t) v].uPan * p.width, -1.0f, 1.0f);
            const float angle = (pan + 1.0f) * pi * 0.25f;
            panL[(size_t) v] = std::cos (angle);
            panR[(size_t) v] = std::sin (angle);
        }

        constexpr float actCoef = 0.002f;   // voice fade in/out when count changes

        for (int i = 0; i < n; ++i)
        {
            const float d  = detune.next();
            const float t  = timing.next();
            const float vb = vib.next();
            const float nm = norm.next();
            const float x  = in[i];

            float l = 0.0f, r = 0.0f;

            for (int v = 0; v < maxVoices; ++v)
            {
                auto& vc = voices[(size_t) v];
                const bool  on     = v < nv;
                const float target = on ? 1.0f : 0.0f;
                vc.act += (target - vc.act) * actCoef;

                if (! on && vc.act < 1.0e-4f)
                {
                    vc.act = 0.0f;
                    vc.push (x);              // keep history so re-enabling is seamless
                    continue;
                }

                const float y = vc.tick (x, d, t, vb, p.vibratoRate, windowSize, invSr)
                                * vc.gain * vc.act;
                l += y * panL[(size_t) v];
                r += y * panR[(size_t) v];
            }

            outL[i] = l * nm;
            outR[i] = r * nm;
        }
    }

private:
    static constexpr float twoPi = 6.28318530717959f;

    struct Smooth
    {
        float cur = 0.0f, tgt = 0.0f, coef = 0.001f;
        void  snap (float v) { cur = tgt = v; }
        float next() { cur += coef * (tgt - cur); return cur; }
    };

    struct Voice
    {
        std::vector<float> buf;
        int   size = 0, writePos = 0;
        float uDetune = 0, uTiming = 0, uPan = 0, gain = 1, act = 0;
        float vibRateMul = 1, vibPhase = 0;
        float d1 = 0, d2 = 0, dRate1 = 0.1f, dRate2 = 0.3f;
        float lpCoef = 0.5f, lp = 0, ramp = 0;

        template <typename Rnd>
        void init (int bufSize, Rnd& rnd)
        {
            size = bufSize;
            buf.assign ((size_t) size, 0.0f);
            writePos = 0;
            uDetune    = rnd() * 2.0f - 1.0f;
            uTiming    = rnd();
            uPan       = rnd() * 2.0f - 1.0f;
            gain       = std::pow (10.0f, ((rnd() * 2.0f - 1.0f) * 2.5f) / 20.0f);   // +/-2.5 dB
            vibRateMul = 0.85f + 0.30f * rnd();
            vibPhase   = rnd() * twoPi;
            d1 = rnd() * twoPi;  d2 = rnd() * twoPi;
            dRate1 = 0.07f + 0.12f * rnd();
            dRate2 = 0.21f + 0.22f * rnd();
            ramp   = rnd();
            act    = 0.0f;
            lp     = 0.0f;
            // per-voice brightness: low-pass between ~5 kHz and ~12 kHz (at 48k; scales with sr)
            lpCoef = 1.0f - std::exp (-twoPi * (5000.0f + 7000.0f * rnd()) / 48000.0f);
        }

        void push (float in)
        {
            buf[(size_t) writePos] = in;
            if (++writePos >= size) writePos = 0;
        }

        float read (float delay) const
        {
            float pos = (float) writePos - delay;
            if (pos < 0.0f) pos += (float) size;
            int i0 = (int) pos;
            const float f = pos - (float) i0;
            if (i0 >= size) i0 -= size;
            int i1 = i0 + 1;
            if (i1 >= size) i1 = 0;
            return buf[(size_t) i0] + f * (buf[(size_t) i1] - buf[(size_t) i0]);
        }

        float tick (float in, float detuneCents, float timingSamples, float vibCents,
                    float vibRate, float W, float invSr)
        {
            buf[(size_t) writePos] = in;

            vibPhase += twoPi * vibRate * vibRateMul * invSr;  if (vibPhase > twoPi) vibPhase -= twoPi;
            d1       += twoPi * dRate1 * invSr;                if (d1 > twoPi)       d1       -= twoPi;
            d2       += twoPi * dRate2 * invSr;                if (d2 > twoPi)       d2       -= twoPi;

            const float cents = uDetune * detuneCents
                              + vibCents * std::sin (vibPhase)
                              + 2.0f * (std::sin (d1) + std::sin (d2));      // +/-4 cents slow drift
            const float ratio = std::exp2 (cents / 1200.0f);

            // read position moves at `ratio` x real time -> pitch shift by `ratio`
            ramp += (1.0f - ratio) / W;
            ramp -= std::floor (ramp);
            float rampB = ramp + 0.5f;
            if (rampB >= 1.0f) rampB -= 1.0f;

            const float base = 2.0f + uTiming * timingSamples;
            const float wA   = 0.5f - 0.5f * std::cos (twoPi * ramp);        // Hann; wB = 1 - wA
            const float y    = read (base + ramp * W) * wA + read (base + rampB * W) * (1.0f - wA);

            if (++writePos >= size) writePos = 0;

            lp += lpCoef * (y - lp);
            return lp;
        }
    };

    std::array<Voice, maxVoices> voices;
    Smooth detune, timing, vib, norm;
    float sr = 48000.0f, invSr = 1.0f / 48000.0f, windowSize = 2400.0f;
    bool  prepared = false;
};
