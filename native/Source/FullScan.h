#pragma once
#include "AudioPlanner.h"
#include <juce_core/juce_core.h>

namespace intent {
class FullScan {
public:
    struct Recording {
        std::unique_ptr<juce::TemporaryFile> file;
        int sampleRate, channels;
        std::int64_t frames;
    };

    void prepare(double rate) noexcept;
    bool start(double duration);
    void cancel() noexcept;
    bool wantsAudio() const noexcept;
    bool complete() const noexcept;
    bool failed() const noexcept;
    double progress() const noexcept;
    void record(const float* const* audio, int channels, int frames, std::int64_t timelineFrame) noexcept;
    std::optional<Recording> collect();
    std::optional<AudioPlan> plan(const Recording&, Settings) const;

private:
    using StereoFrame = std::array<float, 2>;
    enum State { idle, armed, writing, captured, interrupted, cancelled, cancelling };
    static constexpr size_t capacity = 1u << 19;
    static_assert(std::atomic<std::int64_t>::is_always_lock_free);
    std::atomic<State> state {idle};
    std::atomic<int> liveRate {0};
    std::atomic<std::int64_t> produced {0}, consumed {0};
    std::vector<StereoFrame> ring;
    int scanRate = 0, scanChannels = 0;
    std::int64_t length = 0;
};
}
