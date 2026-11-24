#include "TrackBalance.h"
#include "LoudnessMeter.h"
#include <chrono>
#include <limits>

namespace intent {
TrackBalance::TrackBalance() = default;
TrackBalance::~TrackBalance() { cancel(); }

bool TrackBalance::available(const Api& a, ReaProject* p) {
    return p && a.master && a.hostTrack && a.hostTrack == a.master(p)
        && a.trackCount && a.track && a.parent && a.get && a.set && a.items && a.effects && a.envelopes
        && a.sends && a.revision && a.access && a.close && a.begin && a.end && a.samples
        && a.beginUndo && a.endUndo && a.refresh;
}

bool TrackBalance::eligible(MediaTrack* track) const {
    if (api.items(track) <= 0 || api.effects(track) != 0 || api.envelopes(track) != 0
        || api.sends(track, 0) != 0 || api.sends(track, -1) != 0 || api.sends(track, 1) != 0
        || api.get(track, "I_FOLDERDEPTH") > 0)
        return false;
    for (auto* parent = track; parent && parent != api.hostTrack; parent = api.parent(parent))
        if (api.get(parent, "B_MUTE") > 0 || api.get(parent, "B_MAINSEND") <= 0)
            return false;
    return true;
}

TrackBalance::State TrackBalance::start(const Api& functions, ReaProject* context) {
    cancel();
    changed = 0;
    if (!available(functions, context))
        return State::failed;
    api = functions;
    project = context;
    revision = api.revision(project);
    for (int i = 0, count = api.trackCount(project); i < count; ++i) {
        auto* track = api.track(project, i);
        if (!track)
            continue;
        if (api.get(track, "I_SOLO") > 0) {
            cancel();
            return State::soloed;
        }
        if (eligible(track)) {
            const double gain = api.get(track, "D_VOL");
            if (std::isfinite(gain) && gain > 0.0)
                tracks.push_back({track, gain});
        }
    }
    if (tracks.size() < 2) {
        cancel();
        return State::notEnoughAudio;
    }
    return State::measuring;
}

void TrackBalance::closeTrack() {
    if (accessor)
        api.close(accessor);
    accessor = nullptr;
    meter.reset();
    frames = position = 0;
}

void TrackBalance::cancel() {
    closeTrack();
    tracks.clear();
    project = nullptr;
    next = 0;
}

void TrackBalance::beginTrack() {
    accessor = api.access(tracks[next].handle);
    if (!accessor)
        return;
    startTime = std::max(0.0, api.begin(accessor));
    const auto duration = api.end(accessor) - startTime;
    if (!std::isfinite(duration) || duration <= 0
        || duration > static_cast<double>(std::numeric_limits<std::int64_t>::max() / sampleRate / 2)) {
        closeTrack();
        return;
    }
    frames = static_cast<std::int64_t>(std::ceil(duration * sampleRate));
    meter = std::make_unique<LoudnessMeter>(sampleRate, 2);
}

TrackBalance::State TrackBalance::step() {
    if (!project)
        return State::idle;
    if (api.revision(project) != revision) {
        cancel();
        return State::interrupted;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(6);
    do {
        if (next == tracks.size())
            return finish();
        if (!accessor)
            beginTrack();
        if (!accessor) {
            ++next;
            continue;
        }
        const int count = static_cast<int>(std::min<std::int64_t>(blockSize, frames - position));
        const auto time = startTime + static_cast<double>(position) / sampleRate;
        const int result = api.samples(accessor, sampleRate, 2, time, count, captured.data());
        if (result < 0) {
            cancel();
            return State::failed;
        }
        for (int i = 0; i < count * 2; ++i)
            audio[i] = result > 0 && std::isfinite(captured[i]) ? static_cast<float>(captured[i]) : 0.0f;
        meter->add(audio.data(), static_cast<size_t>(count));
        position += count;
        if (position == frames) {
            tracks[next].lufs = meter->finish().lufs.value_or(-100.0f);
            closeTrack();
            ++next;
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return State::measuring;
}

TrackBalance::State TrackBalance::finish() {
    std::vector<float> levels;
    for (const auto& track : tracks)
        if (track.lufs > -55.0f)
            levels.push_back(track.lufs);
    if (levels.size() < 2) {
        cancel();
        return State::notEnoughAudio;
    }
    std::sort(levels.begin(), levels.end());
    const auto median = (levels[(levels.size() - 1) / 2] + levels[levels.size() / 2]) * 0.5f;

    struct Change { MediaTrack* track; double before, after; };
    std::vector<Change> changes;
    for (const auto& track : tracks) {
        if (track.lufs <= -55.0f)
            continue;
        const auto trimDb = std::clamp(median - track.lufs, -6.0f, 0.0f);
        const auto gain = std::pow(10.0, trimDb / 20.0);
        if (gain < track.gain * 0.9943 && trimDb < 0.0f)
            changes.push_back({track.handle, track.gain, gain});
    }
    if (changes.empty()) {
        cancel();
        return State::alreadyBalanced;
    }
    if (api.revision(project) != revision) {
        cancel();
        return State::interrupted;
    }

    api.beginUndo(project);
    for (const auto& change : changes) {
        if (!api.set(change.track, "D_VOL", change.after)) {
            while (changed > 0) {
                const auto& applied = changes[static_cast<size_t>(--changed)];
                api.set(applied.track, "D_VOL", applied.before);
            }
            api.endUndo(project, "Intent: track balance failed", -1);
            api.refresh();
            cancel();
            return State::failed;
        }
        ++changed;
    }
    api.endUndo(project, "Intent: balance tracks", -1);
    api.refresh();
    cancel();
    return State::applied;
}

double TrackBalance::progress() const {
    if (tracks.empty())
        return 0.0;
    const auto fraction = frames > 0 ? static_cast<double>(position) / static_cast<double>(frames) : 0.0;
    return (static_cast<double>(next) + fraction) / static_cast<double>(tracks.size());
}
}
