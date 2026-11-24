#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace intent {
struct Settings {
    float warmth = 50, punch = 0, width = 50, air = 50;
    float outputDb = 0, ceilingDb = -1;
    bool guard = false, bypass = false;
};

// Fixed-memory DSP. No dependency on JUCE, the UI, or the planning engine.
// Peak guard is a sample-domain hard clip, NOT a true-peak/look-ahead limiter.
class Engine {
public:
    void prepare(double rate) noexcept {
        sampleRate = std::isfinite(rate) && rate >= 8000 ? rate : 44100;
        lowCoeff = coefficient(180);
        highCoeff = coefficient(std::min(6000.0, sampleRate * 0.35));
        sideCoeff = coefficient(140);
        smooth = 1.0f - static_cast<float>(std::exp(-1.0 / (0.02 * sampleRate)));
        attack = static_cast<float>(std::exp(-1.0 / (0.012 * sampleRate)));
        release = static_cast<float>(std::exp(-1.0 / (0.12 * sampleRate)));
        reset();
    }
    void reset() noexcept {
        low = {}; high = {}; sideLow = 0; compressorGain = 1;
        lowGain = highGain = outputGain = stereoWidth = 1;
        wet = 1; ratio = 1; guardMix = 0; ceiling = dbToGain(-1);
    }
    void process(float* const* channels, int count, int samples, const Settings& s) noexcept {
        if (count < 1 || count > 2 || samples <= 0) return;
        const auto lowTarget = dbToGain((bounded(s.warmth, 0, 100, 50) - 50) * 0.04f);
        const auto highTarget = dbToGain((bounded(s.air, 0, 100, 50) - 50) * 0.05f);
        const auto widthTarget = 0.8f + bounded(s.width, 0, 100, 50) * 0.004f;
        const auto outputTarget = dbToGain(bounded(s.outputDb, -24, 12, 0));
        const auto ratioTarget = 1.0f + bounded(s.punch, 0, 100, 0) * 0.02f;
        const auto ceilingTarget = dbToGain(bounded(s.ceilingDb, -3, -0.1f, -1));
        for (int i = 0; i < samples; ++i) {
            lowGain += smooth * (lowTarget - lowGain);
            highGain += smooth * (highTarget - highGain);
            stereoWidth += smooth * (widthTarget - stereoWidth);
            outputGain += smooth * (outputTarget - outputGain);
            ratio += smooth * (ratioTarget - ratio);
            ceiling += smooth * (ceilingTarget - ceiling);
            guardMix += smooth * ((s.guard ? 1.0f : 0.0f) - guardMix);
            wet += smooth * ((s.bypass ? 0.0f : 1.0f) - wet);
            std::array<float, 2> dry {}, signal {};
            for (int c = 0; c < count; ++c) {
                dry[c] = clean(channels[c][i]);
                low[c] += lowCoeff * (dry[c] - low[c]);
                high[c] += highCoeff * (dry[c] - high[c]);
                signal[c] = dry[c] + (lowGain - 1) * low[c]
                           + (highGain - 1) * (dry[c] - high[c]);
            }
            if (count == 2) {
                const float mid = (signal[0] + signal[1]) * 0.5f;
                const float side = (signal[0] - signal[1]) * 0.5f;
                sideLow += sideCoeff * (side - sideLow);
                // Only modify the upper side band; leave existing bass image alone.
                const float shapedSide = side + (stereoWidth - 1) * (side - sideLow);
                signal[0] = mid + shapedSide; signal[1] = mid - shapedSide;
            }
            const float peak = std::max(std::abs(signal[0]), std::abs(signal[1]));
            const float overDb = std::max(0.0f, 20.0f * std::log10(std::max(peak, 1.0e-9f)) + 18.0f);
            const float targetGain = dbToGain(-overDb * (1.0f - 1.0f / ratio));
            const float time = targetGain < compressorGain ? attack : release;
            compressorGain = time * compressorGain + (1 - time) * targetGain;
            for (int c = 0; c < count; ++c) {
                float value = signal[c] * compressorGain * outputGain;
                const float clipped = std::clamp(value, -ceiling, ceiling);
                value += guardMix * (clipped - value);
                // Once engaged, enforce the ceiling even while the enable ramp settles.
                if (s.guard) value = std::clamp(value, -ceilingTarget, ceilingTarget);
                channels[c][i] = clean(dry[c] + wet * (value - dry[c]));
            }
        }
    }
    static float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
private:
    static float bounded(float x, float lo, float hi, float fallback) noexcept {
        return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
    }
    static float clean(float x) noexcept {
        return std::isfinite(x) ? std::clamp(x, -32.0f, 32.0f) : 0.0f;
    }
    float coefficient(double hz) const noexcept {
        return 1.0f - static_cast<float>(std::exp(-6.283185307179586 * hz / sampleRate));
    }
    double sampleRate = 44100;
    std::array<float, 2> low {}, high {};
    float lowCoeff {}, highCoeff {}, sideCoeff {}, sideLow {}, smooth {}, attack {}, release {};
    float lowGain = 1, highGain = 1, outputGain = 1, stereoWidth = 1;
    float compressorGain = 1, wet = 1, ratio = 1, guardMix = 0, ceiling = 1;
};
}
