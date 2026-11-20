#pragma once
// YIN pitch detector (de Cheveigne & Kawahara, 2002), JUCE-free.
// Input is decimated to ~24 kHz internally. The lowest detectable pitch can be changed
// at run time with setMinFreq(): a higher minimum means a shorter analysis window,
// which means faster, lower-latency tracking.

#include <algorithm>
#include <cmath>
#include <vector>

class PitchDetector
{
public:
    // Allocates for the worst case (60 Hz). No allocation happens after this.
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) std::lround (sampleRate / 24000.0));
        fs    = (float) (sampleRate / decim);
        maxTauMax = (int) (fs / 60.0f) + 2;
        maxW = 512;

        ring.assign ((size_t) (maxW + maxTauMax), 0.0f);
        frame.assign ((size_t) (maxW + maxTauMax), 0.0f);
        d.assign ((size_t) maxTauMax + 1, 0.0f);
        cm.assign ((size_t) maxTauMax + 1, 1.0f);
        setMinFreq (65.0f);
    }

    void setMinFreq (float minHz)
    {
        tauMin = std::max (2, (int) (fs / 1000.0f));                    // up to 1 kHz
        tauMax = std::min (maxTauMax, (int) (fs / minHz) + 2);
        W      = std::clamp ((int) (1.4f * (float) tauMax), 192, maxW);
        frameLen = W + tauMax;
        std::fill (ring.begin(), ring.end(), 0.0f);
        wp = 0; cnt = 0; acc = 0.0f;
    }

    void push (float x)
    {
        acc += x;
        if (++cnt >= decim)
        {
            ring[(size_t) wp] = acc / (float) decim;
            if (++wp >= frameLen) wp = 0;
            acc = 0.0f;
            cnt = 0;
        }
    }

    // Returns f0 in Hz, or 0 when unvoiced / silent. `clarity` is 0..1.
    float detect (float& clarity)
    {
        clarity = 0.0f;
        for (int i = 0; i < frameLen; ++i)
        {
            int idx = wp + i;
            if (idx >= frameLen) idx -= frameLen;
            frame[(size_t) i] = ring[(size_t) idx];
        }

        const float* x = frame.data() + tauMax;     // most recent W samples

        float energy = 0.0f;
        for (int j = 0; j < W; ++j) energy += x[j] * x[j];
        if (energy / (float) W < 4.0e-6f)           // below about -54 dBFS rms
            return 0.0f;

        d[0] = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float sum = 0.0f;
            const float* y = x - tau;
            for (int j = 0; j < W; ++j)
            {
                const float diff = x[j] - y[j];
                sum += diff * diff;
            }
            d[(size_t) tau] = sum;
        }

        cm[0] = 1.0f;
        float running = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += d[(size_t) tau];
            cm[(size_t) tau] = running > 0.0f ? d[(size_t) tau] * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
        {
            int mn = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cm[(size_t) tau] < cm[(size_t) mn]) mn = tau;
            if (cm[(size_t) mn] < 0.35f) best = mn; else return 0.0f;
        }

        float tauF = (float) best;
        if (best > 1 && best < tauMax - 1)
        {
            const float a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const float denom = a - 2.0f * b + c;
            if (std::abs (denom) > 1.0e-9f)
                tauF += 0.5f * (a - c) / denom;
        }

        clarity = 1.0f - cm[(size_t) best];
        return fs / tauF;
    }

private:
    int decim = 2, tauMin = 24, tauMax = 372, W = 512, frameLen = 884, maxTauMax = 402, maxW = 512;
    float fs = 24000.0f;
    std::vector<float> ring, frame, d, cm;
    int wp = 0, cnt = 0;
    float acc = 0.0f;
};
