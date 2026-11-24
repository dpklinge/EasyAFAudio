#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "LanguagePlanner.h"
#include <future>

class IntentEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit IntentEditor(IntentProcessor&);
    ~IntentEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void startDirection();
    void startAnalysis();
    void pollCapture();
    void cancelAnalysis();
    bool launchPlan(bool fullScan);
    void rememberControls();
    bool requestIsCurrent() const;
    void finishDirection();
    IntentProcessor& processor;
    juce::LookAndFeel_V4 look;
    juce::ComboBox genre;
    juce::TextEditor prompt;
    juce::TextButton apply {"Build direction"};
    juce::TextButton analyze {"Analyze & Build"};
    juce::TextButton resetDefaults {"Reset to genre defaults"};
    juce::Label subtitle, version, promptLabel, status, meter, warning, comparison;
    std::array<juce::Slider,6> sliders;
    std::array<juce::Label,6> labels;
    juce::ToggleButton guard {"Sample peak guard (hard clip)"}, bypass {"Bypass"};
    using SliderAttachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment=juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::array<std::unique_ptr<SliderAttachment>,6> attachments;
    std::unique_ptr<ButtonAttachment> guardAttachment,bypassAttachment;
    struct Result {
        intent::Plan direction;
        std::optional<intent::AudioPlan> audio;
    };
    std::future<Result> pendingPlan;
    juce::String requestedPrompt;
    int requestedGenre = 0;
    unsigned requestedStateRevision = 0;
    intent::Settings requestedSettings;
    std::chrono::steady_clock::time_point lastScanActivity;
    double lastScanProgress = 0.0;
    bool capturing = false, analyzing = false;
    bool directionCancelled = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IntentEditor)
};
