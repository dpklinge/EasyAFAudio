#pragma once
#include "IntentDsp.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace intent {
struct AudioMetrics {
    float rmsDb, peakDb, lowDb, highDb, correlation;
    std::optional<double> flatTopSeconds, overloadSeconds;
    std::optional<float> lufs, truePeakDb;
};

inline float powerDb(double power) {
    return static_cast<float>(10.0 * std::log10(std::max(power, 1.0e-16)));
}

class MetricsAccumulator {
public:
    MetricsAccumulator(int rate, int count, bool inspectInput = false)
        : sampleRate(rate), channels(count), inspectInput(inspectInput),
          lowCoeff(1.0 - std::exp(-6.283185307 * 180.0 / rate)),
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
                if (inspectInput) {
                    const auto frame = frames + i;
                    if (std::abs(sample) > 1.000001)
                        keepEarliest(firstOverload, frame);
                    inspectPeak(channel, static_cast<float>(sample), frame);
                }
            }
        }
        frames += count;
    }

    AudioMetrics metrics() const {
        const double total = std::max(energy[0] + energy[1], 1.0e-16);
        return {powerDb(total / static_cast<double>(std::max<std::int64_t>(frames * channels, 1))), powerDb(peak),
                powerDb(lowEnergy / total), powerDb(highEnergy / total),
                channels == 1 ? 1.0f
                    : static_cast<float>(cross / std::sqrt(std::max(energy[0] * energy[1], 1.0e-32))),
                seconds(firstFlatTop), seconds(firstOverload), {}, {}};
    }

private:
    struct PeakRun {
        float level = 0.0f;
        std::int64_t start = 0, count = 0;
        bool enteredFromBelow = false;
    };
    struct PreviousPeak {
        float level = 0.0f;
        std::int64_t start = -1;
    };

    static void keepEarliest(std::optional<std::int64_t>& first, std::int64_t frame) {
        if (!first || frame < *first)
            first = frame;
    }
    std::optional<double> seconds(std::optional<std::int64_t> first) const {
        if (!first)
            return {};
        return static_cast<double>(*first) / sampleRate;
    }
    void inspectPeak(int channel, float sample, std::int64_t frame) {
        auto& run = runs[channel];
        const float magnitude = std::abs(run.level);
        const float tolerance = std::max(magnitude * 2e-7f, 1e-8f);
        if (run.count > 0 && std::abs(sample - run.level) <= tolerance) {
            ++run.count;
            return;
        }
        const float polarity = run.level < 0 ? -1.0f : 1.0f;
        if (magnitude >= 0.03f && run.count >= 3 && run.enteredFromBelow
            && polarity * sample < magnitude - tolerance * 16.0f) {
            auto& previous = peaks[channel][run.level < 0 ? 1 : 0];
            // A repeated, near-identical plateau at a local extremum is stronger evidence
            // of hard clipping than a single flat sample or a large floating-point peak.
            if (previous.start >= 0 && run.start - previous.start <= sampleRate / 4
                && std::abs(previous.level - run.level) <= magnitude * 1e-5f)
                keepEarliest(firstFlatTop, previous.start);
            previous = {run.level, run.start};
        }
        const float samplePolarity = sample < 0 ? -1.0f : 1.0f;
        run = {sample, frame, 1, run.count > 0
            && samplePolarity * (sample - run.level) > std::max(std::abs(sample) * 3.2e-6f, 1.6e-7f)};
    }

    int sampleRate, channels;
    bool inspectInput;
    double lowCoeff, highCoeff;
    std::int64_t frames = 0;
    std::array<double, 2> energy {}, lows {}, highs {};
    std::array<PeakRun, 2> runs {};
    std::array<std::array<PreviousPeak, 2>, 2> peaks {};
    std::optional<std::int64_t> firstFlatTop, firstOverload;
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
    const float levelMatch = before->lufs && probe->lufs
        ? *before->lufs - *probe->lufs : before->rmsDb - probe->rmsDb;
    const float peak = std::max(probe->peakDb, probe->truePeakDb.value_or(probe->peakDb));
    settings.outputDb = std::clamp(std::min(levelMatch, ceiling - peak),
                                   -24.0f, 6.0f);
    if (const auto after = measure(&settings))
        return AudioPlan{settings, *before, *after};
    return {};
}
}
