#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "IntentDsp.h"
#include "FullScan.h"
#include "ReaperTransport.h"

static_assert(std::atomic<float>::is_always_lock_free, "Audio-thread parameters require lock-free float atomics");
namespace intent { struct Plan; }

class IntentProcessor final : public juce::AudioProcessor {
public:
    IntentProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override { capture.prepare(0); }
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Intent"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &transport; }
    juce::String applyDirection(int genre, const juce::String& prompt);
    void applyDirectionPlan(int genre, const juce::String& prompt, const intent::Plan&);
    void applyAudioPlan(int genre, const juce::String& prompt, const intent::AudioPlan&);
    intent::Settings currentSettings() const noexcept;
    unsigned getStateRestoreRevision() const noexcept { return stateRestoreRevision.load(); }
    void resetToGenreDefaults(int genre);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float> inputPeak {0}, outputPeak {0};
    intent::FullScan capture;
    ReaperTransport transport;
private:
    void applySettings(const intent::Settings&, int controlCount);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void process(juce::AudioBuffer<float>&, bool forceBypass);
    intent::Engine engine;
    std::atomic<unsigned> stateRestoreRevision {0};
    std::array<std::atomic<float>*, 8> raw {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntentProcessor)
};
