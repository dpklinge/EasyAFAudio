#pragma once
#include "IntentDsp.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace intent {
struct AudioMetrics {
    float rmsDb, peakDb, lowDb, highDb, correlation;
};

inline float powerDb(double power) {
    return static_cast<float>(10.0 * std::log10(std::max(power, 1.0e-16)));
}

class MetricsAccumulator {
public:
    MetricsAccumulator(int rate, int count)
        : channels(count), lowCoeff(1.0 - std::exp(-6.283185307 * 180.0 / rate)),
          highCoeff(1.0 - std::exp(-6.283185307 * std::min(6000.0, rate * 0.35) / rate)) {}

    void add(const float* const* audio, int count) {
        for (int channel = 0; channel < channels; ++channel) {
            auto& low = lows[channel];
            auto& high = highs[channel];
            for (int i = 0; i < count; ++i) {
                const double sample = audio[channel][i];
                low += lowCoeff * (sample - low);
                high += highCoeff * (sample - high);
                energy[channel] += sample * sample;
                peak = std::max(peak, sample * sample);
                lowEnergy += low * low;
                highEnergy += (sample - high) * (sample - high);
                if (channel == 1)
                    cross += sample * audio[0][i];
            }
        }
        frames += count;
    }

    AudioMetrics metrics() const {
        const double total = std::max(energy[0] + energy[1], 1.0e-16);
        return {powerDb(total / static_cast<double>(std::max<std::int64_t>(frames * channels, 1))), powerDb(peak),
                powerDb(lowEnergy / total), powerDb(highEnergy / total),
                channels == 1 ? 1.0f
                    : static_cast<float>(cross / std::sqrt(std::max(energy[0] * energy[1], 1.0e-32)))};
    }

private:
    int channels;
    double lowCoeff, highCoeff;
    std::int64_t frames = 0;
    std::array<double, 2> energy {}, lows {}, highs {};
    double peak = 0, lowEnergy = 0, highEnergy = 0, cross = 0;
};

struct AudioPlan {
    Settings settings;
    AudioMetrics before, after;
};

template <typename MeasurePass>
std::optional<AudioPlan> buildAudioPlan(Settings settings, int channels, MeasurePass measure) {
    const auto before = measure(nullptr);
    if (!before || before->rmsDb < -65.0f)
        return {};
    const auto shift = [](float current, float correction) {
        return std::clamp(current + correction, 0.0f, 100.0f);
    };
    settings.warmth = shift(settings.warmth, std::clamp((-9.0f - before->lowDb) * 1.7f, -22.0f, 22.0f));
    settings.air = shift(settings.air, std::clamp((-13.0f - before->highDb) * 1.6f, -22.0f, 22.0f));
    const float crest = before->peakDb - before->rmsDb;
    settings.punch = shift(settings.punch, std::clamp((crest - 12.0f) * 3.0f, -28.0f, 24.0f));
    if (channels == 1)
        settings.width = 50.0f;
    else if (before->correlation < 0.25f)
        settings.width = std::min(settings.width, 45.0f);
    settings.outputDb = 0;
    settings.bypass = false;
    auto trimProbe = settings;
    trimProbe.guard = false;
    const auto probe = measure(&trimProbe);
    if (!probe)
        return {};
    const float ceiling = std::min(-1.0f, settings.ceilingDb);
    settings.outputDb = std::clamp(std::min(before->rmsDb - probe->rmsDb, ceiling - probe->peakDb),
                                   -24.0f, 6.0f);
    if (const auto after = measure(&settings))
        return AudioPlan{settings, *before, *after};
    return {};
}
}
