#pragma once
// HarmonizerEngine: Bon-Iver-style vocal harmonizer, JUCE-free.
//
//  1. Track the input pitch (YIN).
//  2. Snap it to a key/scale (or to the MIDI notes you hold) -> hard-tuned lead.
//  3. Make 4 harmony voices, each N scale-steps from the lead (or one per held MIDI note).
//  4. Each voice is a formant-preserving PSOLA-style shifter: Hann grains two pitch
//     periods long are overlap-added at the new period, so vowels keep their character.
//
// Latency is fixed (see latencySamples()).

#include "PitchDetector.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

class HarmonizerEngine
{
public:
    static constexpr int numHarm   = 4;
    static constexpr int numVoices = numHarm + 1;      // voice 0 = lead

    struct Params
    {
        int   key = 0;                // 0 = C ... 11 = B
        int   scale = 0;              // 0 Major, 1 Minor, 2 Dorian, 3 Mixolydian
        bool  midiMode = false;       // harmonies follow held MIDI notes
        int   range = 1;              // 0 low voice (65 Hz+), 1 mid (100 Hz+), 2 high (150 Hz+)
        float tune = 1.0f;            // 0 = natural, 1 = hard-tuned
        float glideMs = 8.0f;         // how fast pitch snaps to the target
        float leadLevel = 1.0f;
        int   interval[numHarm] = { 2, 4, -3, -7 };            // scale steps from lead
        float level[numHarm]    = { 0.6f, 0.6f, 0.6f, 0.6f };
        float spreadCents = 5.0f;     // random per-voice detune
        float width = 0.7f;           // stereo spread
    };

    void prepare (double sampleRate, unsigned seed = 7)
    {
        sr      = (float) sampleRate;
        t0Min   = sr / 1000.0f;
        detector.prepare (sampleRate);
        curRange = -1;
        applyRange (1);
        t0Cur   = 0.004f * sr;

        ringSize = 1; while ((double) ringSize < 0.4 * sampleRate) ringSize <<= 1;
        ring.assign ((size_t) ringSize, 0.0f);
        ringMask = ringSize - 1;

        accSize = 1; while ((double) accSize < 0.1 * sampleRate) accSize <<= 1;
        accMask = accSize - 1;

        hop = std::max (32, (int) (0.003 * sampleRate));          // 3 ms control rate
        holdHops = std::max (3, (int) std::lround (0.035 * sampleRate / hop));
        hopCount = 0;
        now = 0;

        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
        for (auto& v : voices)
        {
            v = Voice();
            v.acc.assign ((size_t) accSize, 0.0f);
            v.uDetune = uni (rng);
        }
        voicedHold = 0; histCount = 0; lastLead = -1;
    }

    int latencySamples() const { return latency; }
    float debugInputMidi() const { return dbgIn.load(); }
    float debugLeadMidi()  const { return dbgLead.load(); }

    // in: mono. outL/outR: harmonized stereo (overwritten). held: 128 bools of held MIDI notes.
    void process (const float* in, float* outL, float* outR, int n, const Params& p, const bool* held)
    {
        if (p.range != curRange)
            applyRange (p.range);

        const float glideCoef = 1.0f - std::exp (-1.0f / (std::max (1.0f, p.glideMs) * 0.001f * sr));
        const float gainCoef  = 1.0f - std::exp (-1.0f / (0.008f * sr));

        std::array<float, numVoices> panL {}, panR {};
        const float panPos[numVoices] = { 0.0f, -0.8f, 0.8f, -0.4f, 0.4f };
        for (int v = 0; v < numVoices; ++v)
        {
            const float pan = panPos[v] * p.width;
            const float a   = (pan + 1.0f) * (float) kPi * 0.25f;
            panL[(size_t) v] = std::cos (a);
            panR[(size_t) v] = std::sin (a);
        }

        for (int i = 0; i < n; ++i)
        {
            const float x = in[i];
            ring[(size_t) (now & ringMask)] = x;
            detector.push (x);

            if (++hopCount >= hop)
            {
                hopCount = 0;
                control (p, held);
            }

            float l = 0.0f, r = 0.0f;
            for (int vi = 0; vi < numVoices; ++vi)
            {
                Voice& v = voices[(size_t) vi];
                v.semi += glideCoef * (v.semiTgt - v.semi);
                v.gain += gainCoef  * (v.gainTgt - v.gain);

                float& slot = v.acc[(size_t) (now & accMask)];

                if (! v.wantActive && v.gain < 1.0e-4f)
                {
                    v.gain = 0.0f;
                    slot = 0.0f;
                    v.nextCenter = (double) now;
                    v.csValid = false;
                    continue;
                }

                const float ratio = std::exp2 (v.semi / 12.0f);
                const float T0    = t0Cur;
                const float Tout  = T0 / ratio;

                if (v.nextCenter < (double) now - 2.0 * T0)
                    v.nextCenter = (double) now;

                while (v.nextCenter - T0 <= (double) now)
                    spawn (v, T0, Tout);

                const float y = slot * v.gain;
                slot = 0.0f;
                l += y * panL[(size_t) vi];
                r += y * panR[(size_t) vi];
            }

            outL[i] = l;
            outR[i] = r;
            ++now;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr int scales[4][7] = { { 0, 2, 4, 5, 7, 9, 11 },
                                          { 0, 2, 3, 5, 7, 8, 10 },
                                          { 0, 2, 3, 5, 7, 9, 10 },
                                          { 0, 2, 4, 5, 7, 9, 10 } };

    struct Voice
    {
        std::vector<float> acc;
        double nextCenter = 0.0, csPrev = 0.0;
        bool   csValid = false, wantActive = false;
        float  semi = 0.0f, semiTgt = 0.0f, gain = 0.0f, gainTgt = 0.0f, uDetune = 0.0f;
    };

    static int floorDiv (int a, int b)
    {
        int q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    bool inScale (int n, int key, int scale) const
    {
        const int r = n - key - 12 * floorDiv (n - key, 12);
        for (int i = 0; i < 7; ++i) if (scales[scale][i] == r) return true;
        return false;
    }

    int degreeOf (int n, int key, int scale) const
    {
        const int rel = n - key;
        const int o   = floorDiv (rel, 12);
        const int r   = rel - 12 * o;
        for (int i = 0; i < 7; ++i) if (scales[scale][i] == r) return 7 * o + i;
        return 7 * o;
    }

    int noteFromDegree (int d, int key, int scale) const
    {
        const int o = floorDiv (d, 7);
        return key + 12 * o + scales[scale][d - 7 * o];
    }

    // Voice range sets the lowest pitch we track, which sets the grain size and latency:
    //   latency = 2.6 x the longest pitch period (needed so a grain never reads the future).
    void applyRange (int r)
    {
        static const float minHz[3] = { 65.0f, 100.0f, 150.0f };
        curRange = std::clamp (r, 0, 2);
        t0Max    = sr / minHz[curRange];
        latency  = (int) std::ceil (2.6f * t0Max);
        t0Cur    = std::min (t0Cur, t0Max);
        detector.setMinFreq (minHz[curRange]);
        voicedHold = 0; histCount = 0;
        for (auto& v : voices)
        {
            std::fill (v.acc.begin(), v.acc.end(), 0.0f);
            v.csValid = false;
            v.nextCenter = (double) now;
        }
    }

    // 4-point cubic (Catmull-Rom) interpolation: keeps high frequencies intact.
    float readRing (double p) const
    {
        const int64_t ip = (int64_t) std::floor (p);
        const float   f  = (float) (p - (double) ip);
        const float   y0 = ring[(size_t) ((ip - 1) & ringMask)];
        const float   y1 = ring[(size_t) (ip & ringMask)];
        const float   y2 = ring[(size_t) ((ip + 1) & ringMask)];
        const float   y3 = ring[(size_t) ((ip + 2) & ringMask)];
        const float   c1 = 0.5f * (y2 - y0);
        const float   c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float   c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // Add one Hann grain (2 periods long) centred at the voice's next output centre.
    void spawn (Voice& v, float T0, float Tout)
    {
        const double tc      = v.nextCenter;
        const double csIdeal = tc - (double) latency;
        double cs;
        if (! v.csValid || std::abs (csIdeal - v.csPrev) > 4.0 * (double) T0)
            cs = csIdeal;
        else
            cs = v.csPrev + (double) T0 * std::floor ((csIdeal - v.csPrev) / (double) T0 + 0.5);
        v.csPrev  = cs;
        v.csValid = true;

        const int64_t oStart = std::max<int64_t> ((int64_t) std::ceil (tc - (double) T0), now);
        const int64_t oEnd   = (int64_t) std::floor (tc + (double) T0);
        const float   norm   = Tout / T0;
        const double  k      = kPi / (double) T0;

        for (int64_t o = oStart; o <= oEnd; ++o)
        {
            const double u = (double) o - tc;
            const float  w = 0.5f + 0.5f * (float) std::cos (u * k);
            v.acc[(size_t) (o & accMask)] += readRing (cs + u) * w * norm;
        }
        v.nextCenter = tc + (double) Tout;
    }

    // Runs every ~8 ms: detect pitch and recompute every voice's target.
    void control (const Params& p, const bool* held)
    {
        float clarity = 0.0f;
        const float f0 = detector.detect (clarity);

        if (f0 > 0.0f)
        {
            const float m = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
            hist[(size_t) histPos] = m;
            histPos = (histPos + 1) % 3;
            histCount = std::min (3, histCount + 1);

            float a = hist[0], b = histCount > 1 ? hist[1] : a, c = histCount > 2 ? hist[2] : a;
            curM = std::max (std::min (a, b), std::min (std::max (a, b), c));   // median of 3
            voicedHold = holdHops;

            const float tTgt = std::clamp (sr / f0, t0Min, t0Max);
            t0Cur += 0.2f * (tTgt - t0Cur);
        }
        else if (voicedHold > 0)
        {
            --voicedHold;
            if (voicedHold == 0) histCount = 0;
        }

        const bool voiced = voicedHold > 0 && histCount > 0;

        Voice& lead = voices[0];
        lead.wantActive = true;
        lead.gainTgt = p.leadLevel;

        if (! voiced)
        {
            lastLead = -1;
            dbgIn.store (-1.0f); dbgLead.store (-1.0f);
            for (auto& v : voices) v.semiTgt = 0.0f;
            for (int k = 0; k < numHarm; ++k)
            {
                voices[(size_t) k + 1].wantActive = false;
                voices[(size_t) k + 1].gainTgt = 0.0f;
            }
            return;
        }

        const double m = (double) curM;
        const int key = p.key, scale = std::clamp (p.scale, 0, 3);

        std::array<int, 128> heldList {};
        int heldCount = 0;
        bool pcHeld[12] = {};
        for (int n = 0; n < 128; ++n)
            if (held[n]) { heldList[(size_t) heldCount++] = n; pcHeld[n % 12] = true; }

        const bool useMidi = p.midiMode && heldCount > 0;
        auto inSet = [&] (int n) { return useMidi ? pcHeld[((n % 12) + 12) % 12] : inScale (n, key, scale); };

        int cand = -1;
        double bestD = 1.0e9;
        for (int n = (int) std::floor (m) - 2; n <= (int) std::ceil (m) + 2; ++n)
            if (inSet (n) && std::abs ((double) n - m) < bestD) { bestD = std::abs ((double) n - m); cand = n; }

        if (cand >= 0 && lastLead >= 0 && cand != lastLead && inSet (lastLead)
            && std::abs (m - lastLead) < std::abs (m - cand) + 0.3)
            cand = lastLead;                                    // hysteresis against flicker
        if (cand < 0) cand = (int) std::lround (m);
        lastLead = cand;

        dbgIn.store ((float) m);
        dbgLead.store ((float) cand);

        const float tune = std::clamp (p.tune, 0.0f, 1.0f);
        lead.semiTgt = (float) (tune * ((double) cand - m));

        const int leadDeg = useMidi ? 0 : degreeOf (cand, key, scale);

        for (int k = 0; k < numHarm; ++k)
        {
            Voice& v = voices[(size_t) k + 1];
            bool active = true;
            double t;
            if (useMidi)
            {
                active = k < heldCount;
                t = active ? (double) heldList[(size_t) k] : (double) cand;
            }
            else
            {
                t = (double) noteFromDegree (leadDeg + p.interval[k], key, scale);
            }

            const double natural = m + (t - (double) cand);        // keeps the singer's own vibrato
            const double eff     = natural + (double) tune * (t - natural);
            const double semi    = std::clamp (eff - m, -24.0, 24.0);

            v.semiTgt    = (float) semi + p.spreadCents * 0.01f * v.uDetune;
            v.wantActive = active;
            v.gainTgt    = active ? p.level[k] : 0.0f;
        }
    }

    PitchDetector detector;
    std::array<Voice, numVoices> voices;
    std::vector<float> ring;
    int ringSize = 0, accSize = 0, hop = 144, hopCount = 0, latency = 1920, curRange = -1, holdHops = 12;
    int64_t ringMask = 0, accMask = 0, now = 0;
    float sr = 48000.0f, t0Max = 768.0f, t0Min = 48.0f, t0Cur = 288.0f;

    std::array<float, 3> hist {};
    int histPos = 0, histCount = 0, voicedHold = 0, lastLead = -1;
    float curM = 60.0f;

    std::atomic<float> dbgIn { -1.0f }, dbgLead { -1.0f };
};
