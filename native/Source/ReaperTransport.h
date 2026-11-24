#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

struct ReaProject;

class ReaperTransport final : public juce::VST3ClientExtensions {
public:
    struct Api {
        ReaProject* project = nullptr;
        double (*length)(ReaProject*) = nullptr;
        double (*cursor)(ReaProject*) = nullptr;
        double (*rate)(ReaProject*) = nullptr;
        int (*playState)(ReaProject*) = nullptr;
        int (*repeat)(ReaProject*, int) = nullptr;
        void (*seek)(ReaProject*, double, bool, bool) = nullptr;
        void (*play)(ReaProject*) = nullptr;
        void (*stop)(ReaProject*) = nullptr;
    };

    ~ReaperTransport() override;
    void setIHostApplication(Steinberg::FUnknown*) override;
    bool resolveProject() noexcept;
    double length() const { return api.length(api.project); }
    int playState() const { return api.playState(api.project); }
    bool runsAtNormalSpeed() const { return std::abs(api.rate(api.project) - 1.0) < 0.000001; }
    void start();
    void finish();
    void setApi(Api functions) { api = functions; }

private:
    Api api;
    Steinberg::FUnknown* reaperContext = nullptr;
    double savedCursor = 0.0;
    int savedRepeat = 0;
    bool ownsPlayback = false;
};
