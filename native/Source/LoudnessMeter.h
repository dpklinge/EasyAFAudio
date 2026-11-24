#pragma once
#include <ebur128.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>

namespace intent {
// Used only on the offline analysis worker. The histogram keeps storage bounded.
class LoudnessMeter {
public:
    LoudnessMeter(int rate, int channelCount)
        : channels(channelCount) {
        // libebur128 1.2.6 rewrites global gate tables during initialization.
        std::unique_lock lock(tables);
        state = ebur128_init(static_cast<unsigned>(channels), static_cast<unsigned long>(rate),
                            EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK | EBUR128_MODE_HISTOGRAM);
        if (state == nullptr)
            throw std::runtime_error("Could not initialize loudness analysis.");
    }
    ~LoudnessMeter() { ebur128_destroy(&state); }
    LoudnessMeter(const LoudnessMeter&) = delete;
    LoudnessMeter& operator=(const LoudnessMeter&) = delete;

    void add(const float* interleaved, size_t frames) {
        std::shared_lock lock(tables);
        addFrames(interleaved, frames);
    }

    struct Reading {
        std::optional<float> lufs;
        std::optional<float> truePeakDb;
    };

    Reading finish() {
        std::shared_lock lock(tables);
        Reading reading;
        double loudness = 0;
        if (ebur128_loudness_global(state, &loudness) != EBUR128_SUCCESS)
            throw std::runtime_error("Could not read integrated loudness.");
        if (std::isfinite(loudness))
            reading.lufs = static_cast<float>(loudness);

        // Flush delayed true-peak filter taps without including padding in LUFS.
        constexpr size_t paddingFrames = 64;
        const std::array<float, paddingFrames * 2> silence {};
        addFrames(silence.data(), paddingFrames);
        double peak = 0;
        for (int channel = 0; channel < channels; ++channel) {
            double value = 0;
            if (ebur128_true_peak(state, static_cast<unsigned>(channel), &value) != EBUR128_SUCCESS)
                throw std::runtime_error("Could not read true peak.");
            peak = std::max(peak, value);
        }
        if (peak > 0)
            reading.truePeakDb = static_cast<float>(20.0 * std::log10(peak));
        return reading;
    }

private:
    void addFrames(const float* interleaved, size_t frames) {
        if (ebur128_add_frames_float(state, interleaved, frames) != EBUR128_SUCCESS)
            throw std::runtime_error("Could not measure loudness.");
    }

    inline static std::shared_mutex tables;
    int channels;
    ebur128_state* state = nullptr;
};
}
