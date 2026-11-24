#include "ReaperTransport.h"
#include <pluginterfaces/base/funknown.h>

namespace reaper {
using namespace Steinberg;
#include "reaper_vst3_interfaces.h"
}

ReaperTransport::~ReaperTransport() {
    if (reaperContext != nullptr)
        reaperContext->release();
}

void ReaperTransport::setIHostApplication(Steinberg::FUnknown* host) {
    if (host == nullptr)
        return;
    void* result = nullptr;
    if (host->queryInterface(INLINE_UID_OF(reaper::IReaperHostApplication), &result) != Steinberg::kResultOk
        || result == nullptr)
        return;
    auto* reaperHost = static_cast<reaper::IReaperHostApplication*>(result);
    Api functions;
    functions.project = static_cast<ReaProject*>(reaperHost->getReaperParent(3));
    functions.length = reinterpret_cast<decltype(functions.length)>(reaperHost->getReaperApi("GetProjectLength"));
    functions.cursor = reinterpret_cast<decltype(functions.cursor)>(reaperHost->getReaperApi("GetCursorPositionEx"));
    functions.rate = reinterpret_cast<decltype(functions.rate)>(reaperHost->getReaperApi("Master_GetPlayRate"));
    functions.playState = reinterpret_cast<decltype(functions.playState)>(reaperHost->getReaperApi("GetPlayStateEx"));
    functions.repeat = reinterpret_cast<decltype(functions.repeat)>(reaperHost->getReaperApi("GetSetRepeatEx"));
    functions.seek = reinterpret_cast<decltype(functions.seek)>(reaperHost->getReaperApi("SetEditCurPos2"));
    functions.play = reinterpret_cast<decltype(functions.play)>(reaperHost->getReaperApi("OnPlayButtonEx"));
    functions.stop = reinterpret_cast<decltype(functions.stop)>(reaperHost->getReaperApi("OnStopButtonEx"));
    if (reaperContext != nullptr)
        reaperContext->release();
    reaperContext = reaperHost;
    setApi(functions);
}

bool ReaperTransport::resolveProject() noexcept {
    if (reaperContext != nullptr)
        api.project = static_cast<ReaProject*>(static_cast<reaper::IReaperHostApplication*>(reaperContext)->getReaperParent(3));
    return api.project && api.length && api.cursor && api.rate && api.playState
        && api.repeat && api.seek && api.play && api.stop;
}

void ReaperTransport::start() {
    savedCursor = api.cursor(api.project);
    savedRepeat = api.repeat(api.project, -1);
    if (playState() != 0)
        api.stop(api.project);
    api.repeat(api.project, 0);
    api.seek(api.project, 0.0, false, false);
    ownsPlayback = true;
    api.play(api.project);
}

void ReaperTransport::finish() {
    if (!ownsPlayback)
        return;
    ownsPlayback = false;
    if ((playState() & 4) == 0) {
        if (playState() != 0)
            api.stop(api.project);
        api.seek(api.project, savedCursor, false, false);
    }
    api.repeat(api.project, savedRepeat);
}
