#include "FullScan.h"
#include "LoudnessMeter.h"
#include <stdexcept>
#include <thread>

namespace intent {
void FullScan::prepare(double rate) noexcept {
    liveRate.store(std::isfinite(rate) && rate >= 8000 && rate <= 384000
                        ? static_cast<int>(std::lround(rate)) : 0);
}

bool FullScan::start(double duration) {
    const auto phase = state.load();
    const auto rate = liveRate.load();
    if (phase == writing || phase == armed || phase == cancelling || rate == 0
        || !std::isfinite(duration) || duration <= 0
        || duration > static_cast<double>(std::numeric_limits<std::int64_t>::max() / 16 / rate))
        return false;
    length = static_cast<std::int64_t>(std::ceil(duration * rate));
    ring.resize(capacity);
    scanRate = rate;
    scanChannels = 0;
    produced.store(0);
    consumed.store(0);
    state.store(armed);
    return true;
}

void FullScan::cancel() noexcept {
    auto current = state.load();
    while (current == armed || current == writing || current == captured || current == interrupted) {
        if (state.compare_exchange_strong(current, current == writing ? cancelling : cancelled))
            return;
    }
}

bool FullScan::wantsAudio() const noexcept { return state.load(std::memory_order_relaxed) == armed; }
bool FullScan::complete() const noexcept { return state.load() == captured; }
bool FullScan::failed() const noexcept { return state.load() == interrupted; }
double FullScan::progress() const noexcept {
    return length > 0 ? static_cast<double>(produced.load()) / static_cast<double>(length) : 0.0;
}

void FullScan::record(const float* const* audio, int channels, int frames, std::int64_t timelineFrame) noexcept {
    auto expected = armed;
    if (!state.compare_exchange_strong(expected, writing))
        return;
    auto next = armed;
    const auto offset = produced.load(std::memory_order_relaxed);
    if (liveRate.load() != scanRate || (scanChannels != 0 && scanChannels != channels)) {
        next = interrupted;
    } else if (offset != 0 || timelineFrame == 0) {
        const auto count = std::min<std::int64_t>(frames, length - offset);
        const auto backlog = offset - consumed.load(std::memory_order_acquire);
        if (timelineFrame < offset - 1 || timelineFrame > offset + 1
            || backlog + count > static_cast<std::int64_t>(capacity)) {
            next = interrupted;
        } else {
            scanChannels = channels;
            for (std::int64_t i = 0; i < count; ++i)
                ring[static_cast<size_t>(offset + i) % capacity] = {audio[0][i], channels == 2 ? audio[1][i] : 0.0f};
            produced.store(offset + count, std::memory_order_release);
            if (offset + count == length)
                next = captured;
        }
    }
    expected = writing;
    if (!state.compare_exchange_strong(expected, next))
        state.store(cancelled);
}

std::optional<FullScan::Recording> FullScan::collect() {
    auto file = std::make_unique<juce::TemporaryFile>(".intent-scan");
    juce::FileOutputStream out(file->getFile());
    if (!out.openedOk())
        throw std::runtime_error("Could not open temporary scan storage.");

    std::int64_t position = 0;
    for (;;) {
        const auto phase = state.load();
        if (phase == cancelled || phase == cancelling || phase == interrupted)
            return {};
        const auto available = produced.load(std::memory_order_acquire) - position;
        if (available > 0) {
            const auto index = static_cast<size_t>(position) % capacity;
            const auto count = std::min(static_cast<size_t>(available), capacity - index);
            if (!out.write(ring.data() + index, count * sizeof(StereoFrame)))
                throw std::runtime_error("Could not write temporary scan audio.");
            position += static_cast<std::int64_t>(count);
            consumed.store(position, std::memory_order_release);
        } else if (phase == captured && position == length) {
            out.flush();
            if (out.getStatus().failed())
                throw std::runtime_error("Could not save temporary scan audio.");
            return Recording{std::move(file), scanRate, scanChannels, position};
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
    }
}

std::optional<AudioPlan> FullScan::plan(const Recording& recording, Settings settings) const {
    juce::FileInputStream input(recording.file->getFile());
    if (!input.openedOk())
        throw std::runtime_error("Could not read temporary scan audio.");
    constexpr int blockSize = 4096;
    std::array<StereoFrame, blockSize> interleaved;
    std::array<float, blockSize * 2> loudnessInput;
    std::array<std::array<float, blockSize>, 2> audio;
    const std::array<float*, 2> samples {audio[0].data(), audio[1].data()};
    auto measurePass = [&](const Settings* processing) -> std::optional<AudioMetrics> {
        if (!input.setPosition(0))
            throw std::runtime_error("Could not rewind scan audio.");
        Engine engine;
        if (processing != nullptr)
            engine.prepare(recording.sampleRate);
        const auto latency = engine.latencySamples();
        int toSkip = latency;
        MetricsAccumulator meter(recording.sampleRate, recording.channels, processing == nullptr);
        LoudnessMeter loudnessMeter(recording.sampleRate, recording.channels);
        const auto processAndMeasure = [&](int count) {
            if (processing != nullptr)
                engine.process(samples.data(), recording.channels, count, *processing);
            const auto skip = std::min(toSkip, count);
            toSkip -= skip;
            const auto measured = count - skip;
            if (measured == 0)
                return;
            const std::array<const float*, 2> aligned {samples[0] + skip, samples[1] + skip};
            meter.add(aligned.data(), measured);
            for (int i = 0; i < measured; ++i)
                for (int channel = 0; channel < recording.channels; ++channel)
                    loudnessInput[i * recording.channels + channel] = aligned[channel][i];
            loudnessMeter.add(loudnessInput.data(), static_cast<size_t>(measured));
        };
        for (std::int64_t offset = 0; offset < recording.frames;) {
            if (state.load() != captured)
                return {};
            const auto count = static_cast<int>(std::min<std::int64_t>(blockSize, recording.frames - offset));
            const auto bytes = static_cast<int>(count * sizeof(StereoFrame));
            if (input.read(interleaved.data(), bytes) != bytes)
                throw std::runtime_error("Temporary scan audio ended early.");
            for (int i = 0; i < count; ++i) {
                audio[0][i] = interleaved[static_cast<size_t>(i)][0];
                audio[1][i] = interleaved[static_cast<size_t>(i)][1];
            }
            processAndMeasure(count);
            offset += count;
        }
        for (int remaining = latency; remaining > 0;) {
            if (state.load() != captured)
                return {};
            const auto count = std::min(blockSize, remaining);
            for (auto& channel : audio)
                std::fill_n(channel.data(), count, 0.0f);
            processAndMeasure(count);
            remaining -= count;
        }
        auto metrics = meter.metrics();
        const auto reading = loudnessMeter.finish();
        metrics.lufs = reading.lufs;
        metrics.truePeakDb = reading.truePeakDb;
        return metrics;
    };
    return buildAudioPlan(settings, recording.channels, measurePass);
}
}
