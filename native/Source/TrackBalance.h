#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct ReaProject;
struct MediaTrack;
struct AudioAccessor;

namespace intent {
class LoudnessMeter;

// These REAPER calls, including audio accessor reads, run on the message thread.
class TrackBalance {
public:
    struct Api {
        MediaTrack* hostTrack = nullptr;
        MediaTrack* (*master)(ReaProject*) = nullptr;
        int (*trackCount)(ReaProject*) = nullptr;
        MediaTrack* (*track)(ReaProject*, int) = nullptr;
        MediaTrack* (*parent)(MediaTrack*) = nullptr;
        double (*get)(MediaTrack*, const char*) = nullptr;
        bool (*set)(MediaTrack*, const char*, double) = nullptr;
        int (*items)(MediaTrack*) = nullptr;
        int (*effects)(MediaTrack*) = nullptr;
        int (*envelopes)(MediaTrack*) = nullptr;
        int (*sends)(MediaTrack*, int) = nullptr;
        int (*revision)(ReaProject*) = nullptr;
        AudioAccessor* (*access)(MediaTrack*) = nullptr;
        void (*close)(AudioAccessor*) = nullptr;
        double (*begin)(AudioAccessor*) = nullptr;
        double (*end)(AudioAccessor*) = nullptr;
        int (*samples)(AudioAccessor*, int, int, double, int, double*) = nullptr;
        void (*beginUndo)(ReaProject*) = nullptr;
        void (*endUndo)(ReaProject*, const char*, int) = nullptr;
        void (*refresh)() = nullptr;
    };
    enum class State { idle, measuring, applied, alreadyBalanced, notEnoughAudio, soloed, interrupted, failed };

    TrackBalance();
    ~TrackBalance();
    static bool available(const Api&, ReaProject*);
    State start(const Api&, ReaProject*);
    State step();
    void cancel();
    double progress() const;
    int changedTracks() const noexcept { return changed; }

private:
    struct Track { MediaTrack* handle; double gain; float lufs = -100.0f; };
    State finish();
    bool eligible(MediaTrack*) const;
    void closeTrack();
    void beginTrack();
    Api api;
    ReaProject* project = nullptr;
    int revision = 0, changed = 0;
    std::vector<Track> tracks;
    size_t next = 0;
    AudioAccessor* accessor = nullptr;
    std::unique_ptr<LoudnessMeter> meter;
    double startTime = 0;
    std::int64_t position = 0, frames = 0;
    static constexpr int sampleRate = 48000, blockSize = 4096;
    std::array<double, blockSize * 2> captured;
    std::array<float, blockSize * 2> audio;
};
}
