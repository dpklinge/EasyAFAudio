#pragma once

#include <juce_core/juce_core.h>
#include "KeywordPlanner.h"

namespace intent {
// Call on a worker. DSP processing only consumes the resolved Settings.
Plan makeLanguagePlan(int genre, const juce::String& prompt);
}
