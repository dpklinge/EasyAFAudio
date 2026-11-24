#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace intent {
class LookaheadLimiter {
public:
    void prepare(double rate) {
        // Rate-dependent 4x/2x polyphase interpolation keeps the detector at
        // roughly 192 kHz or above at common mastering rates.
        phases = rate < 96000 ? 4 : 2;
        detectorFrames = (filterTaps + phases - 1) / phases;
        constexpr double pi = 3.14159265358979323846;
        coefficients = {};
        for (int tap = 0; tap < filterTaps; ++tap) {
            const double position = (tap - (filterTaps - 1) * 0.5) * pi / phases;
            const double sinc = position == 0.0 ? 1.0 : std::sin(position) / position;
            const double window = 0.5 * (1.0 - std::cos(2.0 * pi * tap / (filterTaps - 1)));
            coefficients[tap % phases][tap / phases] = static_cast<float>(sinc * window);
        }
        delay.resize(static_cast<size_t>(std::ceil(0.005 * rate)) + detectorFrames);
        attack.resize(static_cast<size_t>(std::ceil(0.001 * rate)));
        peakWindow = delay.size() + static_cast<size_t>(std::ceil(0.001 * rate)) + detectorFrames;
        maxima.resize(peakWindow + 2);
        release = static_cast<float>(std::exp(-1.0 / (0.080 * rate)));
        disableSmoothing = static_cast<float>(std::exp(-1.0 / (0.005 * rate)));
        reset();
    }

    void reset() noexcept {
        history = {};
        std::fill(delay.begin(), delay.end(), Frame{});
        std::fill(attack.begin(), attack.end(), 1.0f);
        attackSum = static_cast<double>(attack.size());
        delayIndex = historyIndex = attackIndex = first = pastLast = 0;
        frameIndex = 0;
        gain = 1.0f;
        enabledMix = 0.0f;
    }

    int latencySamples() const noexcept { return static_cast<int>(delay.size()); }

    bool process(float* const* audio, int channels, int frames, float ceilingGain, bool enabled) noexcept {
        if (delay.empty())
            return false;
        bool reducing = false;
        const float detectorCeiling = ceilingGain * 0.971628f; // 0.25 dB reconstruction margin.
        for (int i = 0; i < frames; ++i) {
            if (historyIndex == 0)
                historyIndex = detectorFrames;
            --historyIndex;
            const Frame delayed = delay[delayIndex];
            float peak = 0.0f;
            for (int channel = 0; channel < channels; ++channel) {
                const float sample = audio[channel][i];
                delay[delayIndex][channel] = sample;
                auto& input = history[channel];
                input[historyIndex] = input[historyIndex + detectorFrames] = sample;
                peak = std::max(peak, std::abs(sample));
                for (int phase = 1; phase < phases; ++phase) {
                    float interpolated = 0.0f;
                    for (int tap = 0; tap < detectorFrames; ++tap)
                        interpolated += coefficients[phase][tap] * input[historyIndex + tap];
                    peak = std::max(peak, std::abs(interpolated));
                }
            }
            delayIndex = (delayIndex + 1) % delay.size();
            pushPeak(peak);
            const float windowPeak = maxima[first].peak;
            const float target = windowPeak > detectorCeiling ? detectorCeiling / windowPeak : 1.0f;
            // The finite attack ramp settles while the threatening peak is
            // still in the delay. Hold across the detector's entire FIR support.
            attackSum += static_cast<double>(target) - attack[attackIndex];
            attack[attackIndex] = target;
            attackIndex = (attackIndex + 1) % attack.size();
            const auto attackGain = std::clamp(static_cast<float>(attackSum / attack.size()), 0.0f, 1.0f);
            gain = std::min(attackGain, 1.0f - release * (1.0f - gain));
            enabledMix = enabled ? 1.0f : enabledMix * disableSmoothing;
            if (enabledMix < 1e-5f)
                enabledMix = 0.0f;
            const float applied = 1.0f - enabledMix * (1.0f - gain);
            bool hasOutput = false;
            for (int channel = 0; channel < channels; ++channel) {
                audio[channel][i] = delayed[channel] * applied;
                hasOutput |= delayed[channel] != 0.0f;
            }
            reducing |= enabled && hasOutput && applied < 0.9999f;
            ++frameIndex;
        }
        return reducing;
    }

private:
    using Frame = std::array<float, 2>;
    struct Peak { std::uint64_t index = 0; float peak = 0.0f; };
    static constexpr int filterTaps = 49;
    static constexpr int maxDetectorFrames = 25;

    void pushPeak(float peak) noexcept {
        while (first != pastLast && frameIndex - maxima[first].index >= peakWindow)
            first = (first + 1) % maxima.size();
        while (first != pastLast) {
            const auto previous = pastLast == 0 ? maxima.size() - 1 : pastLast - 1;
            if (maxima[previous].peak > peak)
                break;
            pastLast = previous;
        }
        maxima[pastLast] = {frameIndex, peak};
        pastLast = (pastLast + 1) % maxima.size();
    }

    std::array<std::array<float, maxDetectorFrames>, 4> coefficients {};
    std::array<std::array<float, maxDetectorFrames * 2>, 2> history {};
    std::vector<Frame> delay;
    std::vector<float> attack;
    std::vector<Peak> maxima;
    size_t delayIndex = 0, attackIndex = 0, first = 0, pastLast = 0, peakWindow = 0;
    std::uint64_t frameIndex = 0;
    int phases = 4, detectorFrames = 13, historyIndex = 0;
    double attackSum = 0.0;
    float gain = 1.0f, enabledMix = 0.0f, release = 0.0f, disableSmoothing = 0.0f;
};
}
