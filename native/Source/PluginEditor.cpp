#include "PluginEditor.h"

namespace { const juce::Colour background{0xff101311},text{0xffeef3e9},muted{0xffa2ada1},lime{0xffc0f58a}; }
IntentEditor::IntentEditor(IntentProcessor& p):AudioProcessorEditor(p),processor(p) {
    look.setColour(juce::ResizableWindow::backgroundColourId,background);
    look.setColour(juce::Label::textColourId,text);
    look.setColour(juce::Slider::thumbColourId,lime);
    look.setColour(juce::Slider::rotarySliderFillColourId,lime);
    look.setColour(juce::Slider::textBoxTextColourId,text);
    look.setColour(juce::Slider::textBoxOutlineColourId,juce::Colours::transparentBlack);
    look.setColour(juce::TextButton::buttonColourId,lime);
    look.setColour(juce::TextButton::textColourOffId,background);
    look.setColour(juce::ToggleButton::textColourId,text);
    look.setColour(juce::ToggleButton::tickColourId,lime);
    setLookAndFeel(&look);
    addAndMakeVisible(subtitle); subtitle.setText("NATIVE PROTOTYPE / GENRE-GUIDED PROCESSING",juce::dontSendNotification);
    subtitle.setColour(juce::Label::textColourId,muted);
    addAndMakeVisible(version);
    version.setText("Version " JucePlugin_VersionString, juce::dontSendNotification);
    version.setJustificationType(juce::Justification::centredRight);
    version.setColour(juce::Label::textColourId, muted);
    genre.addItemList({"Electronic","Hip-hop","Pop","Rock","Jazz","Ambient"},1);
    genre.setSelectedItemIndex(static_cast<int>(p.parameters.state.getProperty("genre",0)),juce::dontSendNotification);
    genre.setTextWhenNothingSelected("Choose a genre"); genre.setTooltip("Genre starting point");
    genre.setTitle("Genre"); addAndMakeVisible(genre);
    promptLabel.setText("Describe the direction",juce::dontSendNotification);addAndMakeVisible(promptLabel);
    prompt.setMultiLine(true);prompt.setReturnKeyStartsNewLine(true);prompt.setInputRestrictions(500);
    prompt.setTitle("Sound direction");prompt.setText(p.parameters.state.getProperty("prompt").toString(),false);
    prompt.setColour(juce::TextEditor::backgroundColourId,juce::Colour{0xff20271f});
    prompt.setColour(juce::TextEditor::textColourId,text);
    prompt.onTextChange=[this]{processor.parameters.state.setProperty("prompt",prompt.getText(),nullptr);};
    genre.onChange=[this]{processor.parameters.state.setProperty("genre",genre.getSelectedItemIndex(),nullptr);};
    addAndMakeVisible(prompt);addAndMakeVisible(apply);
    apply.setColour(juce::TextButton::buttonColourId, juce::Colour{0xff283521});
    apply.setColour(juce::TextButton::textColourOffId, lime);
    apply.setTooltip("Apply the genre and description without scanning the project.");
    apply.onClick = [this] { startDirection(); };
    addAndMakeVisible(analyze);
    analyze.setTooltip("Scan the full REAPER project through this insert and build a chain.");
    analyze.onClick = [this] { startAnalysis(); };
    resetDefaults.setColour(juce::TextButton::buttonColourId, juce::Colour{0xff283521});
    resetDefaults.setColour(juce::TextButton::textColourOffId, lime);
    resetDefaults.setTooltip("Restore the selected genre's audio controls and switches. Keep the prompt for Build direction.");
    resetDefaults.onClick = [this] {
        directionCancelled = true;
        cancelAnalysis();
        apply.setEnabled(!pendingPlan.valid());
        comparison.setText({}, juce::dontSendNotification);
        processor.resetToGenreDefaults(genre.getSelectedItemIndex());
        status.setText(genre.getText() + " defaults restored. Use Build direction to apply your keywords.",
                       juce::dontSendNotification);
    };
    addAndMakeVisible(resetDefaults);
    constexpr const char* ids[]={"warmth","punch","width","air","output","ceiling"};
    constexpr const char* names[]={"Warmth","Punch","Width","Air","Output / dB","Ceiling / dBFS"};
    for(size_t i=0;i<sliders.size();++i){
        auto& slider=sliders[i];slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow,false,90,24);
        slider.setTitle(names[i]);slider.setNumDecimalPlacesToDisplay(i<4?0:1);
        slider.setTextValueSuffix(i<4?" %":" dB");
        slider.setDoubleClickReturnValue(true,i==1?0.0:i<4?50.0:i==4?0.0:-1.0);
        addAndMakeVisible(slider);labels[i].setText(names[i],juce::dontSendNotification);
        labels[i].setJustificationType(juce::Justification::centred);addAndMakeVisible(labels[i]);
        attachments[i]=std::make_unique<SliderAttachment>(p.parameters,ids[i],slider);
    }
    addAndMakeVisible(guard);addAndMakeVisible(bypass);
    guardAttachment=std::make_unique<ButtonAttachment>(p.parameters,"guard",guard);
    bypassAttachment=std::make_unique<ButtonAttachment>(p.parameters,"bypass",bypass);
    addAndMakeVisible(status);status.setText("Ready. Choose a direction, or adjust the controls.",juce::dontSendNotification);
    status.setColour(juce::Label::textColourId,muted);
    addAndMakeVisible(meter);meter.setColour(juce::Label::textColourId,lime);
    addAndMakeVisible(comparison);comparison.setColour(juce::Label::textColourId,lime);
    addAndMakeVisible(warning);
#if INTENT_USE_MINILM
    warning.setText("Local text analysis with MiniLM. Audio settings remain editable.", juce::dontSendNotification);
#else
    warning.setText("Keyword planner. MiniLM is available in the Windows x64 release.", juce::dontSendNotification);
#endif
    warning.setColour(juce::Label::textColourId,muted);
    setSize(880,650);timerCallback();startTimerHz(20);
}
IntentEditor::~IntentEditor(){
    stopTimer();
    cancelAnalysis();
    if (pendingPlan.valid())
        pendingPlan.wait();
    setLookAndFeel(nullptr);
}

void IntentEditor::startDirection() {
    if (pendingPlan.valid() || capturing)
        return;
    rememberControls();
    launchPlan(false);
}

void IntentEditor::rememberControls() {
    requestedPrompt = prompt.getText().substring(0, 500);
    requestedGenre = genre.getSelectedItemIndex();
    requestedStateRevision = processor.getStateRestoreRevision();
    directionCancelled = false;
    requestedSettings = processor.currentSettings();
    comparison.setText({}, juce::dontSendNotification);
}

bool IntentEditor::requestIsCurrent() const {
    const auto current = processor.currentSettings();
    return !directionCancelled && requestedGenre == genre.getSelectedItemIndex()
        && requestedPrompt == prompt.getText() && requestedStateRevision == processor.getStateRestoreRevision()
        && current.warmth == requestedSettings.warmth && current.punch == requestedSettings.punch
        && current.width == requestedSettings.width && current.air == requestedSettings.air
        && (!analyzing || (current.outputDb == requestedSettings.outputDb && current.ceilingDb == requestedSettings.ceilingDb
                          && current.guard == requestedSettings.guard && current.bypass == requestedSettings.bypass));
}

void IntentEditor::startAnalysis() {
    if (pendingPlan.valid() && analyzing && !directionCancelled) {
        directionCancelled = true;
        cancelAnalysis();
        status.setText("Analysis canceled.", juce::dontSendNotification);
        return;
    }
    if (pendingPlan.valid())
        return;
    auto& transport = processor.transport;
    if (!transport.resolveProject()) {
        status.setText("Full-project analysis is available in REAPER's integrated VST3 host.", juce::dontSendNotification);
        return;
    }
    if ((transport.playState() & 4) != 0) {
        status.setText("Stop recording before starting the full-project scan.", juce::dontSendNotification);
        return;
    }
    if (!transport.runsAtNormalSpeed()) {
        status.setText("Set REAPER playback speed to 1.0 to scan the project.", juce::dontSendNotification);
        return;
    }
    const auto duration = transport.length();
    if (duration <= 0) {
        status.setText("Add audio or instruments to the REAPER project before scanning.", juce::dontSendNotification);
        return;
    }
    try {
        if (!processor.capture.start(duration)) {
            status.setText("The audio device isn't ready. Check REAPER's audio device and try again.", juce::dontSendNotification);
            return;
        }
    } catch (const std::bad_alloc&) {
        status.setText("Could not allocate audio capture. Try again.", juce::dontSendNotification);
        return;
    }
    rememberControls();
    if (!launchPlan(true)) {
        processor.capture.cancel();
        return;
    }
    capturing = true;
    lastScanActivity = std::chrono::steady_clock::now();
    lastScanProgress = 0.0;
    transport.start();
}

void IntentEditor::pollCapture() {
    if (!capturing)
        return;
    if (processor.capture.complete()) {
        capturing = false;
        processor.transport.finish();
        status.setText("Measuring and previewing the full project…", juce::dontSendNotification);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto progress = processor.capture.progress();
    if (progress > lastScanProgress) {
        lastScanProgress = progress;
        lastScanActivity = now;
    }
    const auto transportState = processor.transport.playState();
    if (processor.capture.failed() || (transportState & 4) != 0
        || (progress > 0.0 && (transportState & 1) == 0)
        || now - lastScanActivity > std::chrono::seconds(8)) {
        directionCancelled = true;
        cancelAnalysis();
        status.setText("The full scan was interrupted. Run Analyze & Build to start again.", juce::dontSendNotification);
        return;
    }
    status.setText("Scanning full project… " + juce::String(static_cast<int>(progress * 100)) + "%",
                   juce::dontSendNotification);
}

void IntentEditor::cancelAnalysis() {
    processor.capture.cancel();
    capturing = false;
    processor.transport.finish();
    analyze.setButtonText("Analyze & Build");
    analyze.setEnabled(!pendingPlan.valid());
}

bool IntentEditor::launchPlan(bool fullScan) {
    analyzing = fullScan;
    try {
        pendingPlan = std::async(std::launch::async,
            [genreIndex = requestedGenre, words = requestedPrompt, controls = requestedSettings,
             scan = fullScan ? &processor.capture : nullptr] {
                const auto recording = scan ? scan->collect() : std::optional<intent::FullScan::Recording>{};
                if (scan && !recording)
                    return Result{};
                auto direction = intent::makeLanguagePlan(genreIndex, words);
                std::optional<intent::AudioPlan> result;
                if (recording) {
                    auto settings = direction.settings;
                    settings.guard = controls.guard;
                    settings.ceilingDb = controls.ceilingDb;
                    result = scan->plan(*recording, settings);
                }
                return Result{std::move(direction), result};
            });
        apply.setEnabled(false);
        analyze.setEnabled(fullScan);
        analyze.setButtonText(fullScan ? "Cancel analysis" : "Analyze & Build");
        status.setText(fullScan ? "Starting full-project scan…" : "Building direction…",
                       juce::dontSendNotification);
        return true;
    } catch (const std::system_error&) {
        apply.setEnabled(true);
        status.setText("Could not start the planner. Try again.", juce::dontSendNotification);
    }
    return false;
}

void IntentEditor::finishDirection() {
    using namespace std::chrono_literals;
    if (!pendingPlan.valid() || pendingPlan.wait_for(0ms) != std::future_status::ready)
        return;
    try {
        const auto result = pendingPlan.get();
        if (requestIsCurrent()) {
            if (result.audio) {
                const auto& plan = *result.audio;
                processor.applyAudioPlan(requestedGenre, requestedPrompt, plan);
                status.setText(requestedSettings.bypass ? "Chain applied. Turn off Bypass to hear it."
                              : std::abs(plan.after.rmsDb - plan.before.rmsDb) <= 1
                                  ? "Chain applied. Toggle Bypass to compare at similar levels."
                                  : "Chain applied. Peak headroom limited the level match.",
                               juce::dontSendNotification);
                const auto pair = [](float before, float after) {
                    return juce::String(before, 1) + " → " + juce::String(after, 1);
                };
                comparison.setText("Full scan    RMS " + pair(plan.before.rmsDb, plan.after.rmsDb)
                    + " dBFS    /    Sample peak " + pair(plan.before.peakDb, plan.after.peakDb) + " dBFS",
                    juce::dontSendNotification);
            } else if (analyzing) {
                status.setText("The scan found no usable audio at this insert.", juce::dontSendNotification);
            } else {
                processor.applyDirectionPlan(requestedGenre, requestedPrompt, result.direction);
                status.setText(juce::String(result.direction.explanation), juce::dontSendNotification);
            }
        } else if (!directionCancelled) {
            status.setText("The settings changed during processing. Run the planner again to apply them.",
                           juce::dontSendNotification);
        }
    } catch (const std::exception&) {
        if (!directionCancelled)
            status.setText(analyzing ? "Full-project analysis could not read or write temporary audio."
                                    : "Could not complete the plan. Try again.", juce::dontSendNotification);
    }
    if (analyzing)
        cancelAnalysis();
    apply.setEnabled(true);
    analyze.setEnabled(true);
}
void IntentEditor::paint(juce::Graphics& g){
    g.fillAll(background);g.setColour(lime);g.setFont(38.0f);g.drawText("intent /",28,14,220,50,juce::Justification::centredLeft);
    g.setColour(juce::Colour{0xff30382f});g.drawHorizontalLine(90,28.0f,852.0f);g.drawHorizontalLine(535,28.0f,852.0f);
}
void IntentEditor::resized(){
    subtitle.setBounds(250,26,440,30);
    version.setBounds(708,26,144,30);
    genre.setBounds(28,110,225,36);
    promptLabel.setBounds(28,158,225,24);prompt.setBounds(28,188,225,125);
    for(int i=0;i<6;++i){const int x=282+(i%3)*190,y=108+(i/3)*200;labels[static_cast<size_t>(i)].setBounds(x,y,175,24);sliders[static_cast<size_t>(i)].setBounds(x,y+26,175,155);}
    analyze.setBounds(28,330,225,38);
    apply.setBounds(28,378,225,38);
    resetDefaults.setBounds(28,424,225,35);
    guard.setBounds(28,464,245,30);bypass.setBounds(28,495,225,30);
    comparison.setBounds(282,500,568,25);
    meter.setBounds(28,548,824,26);status.setBounds(28,580,824,30);warning.setBounds(28,616,824,24);
}
void IntentEditor::timerCallback(){
    auto asDb=[](float gain){return gain>0?juce::String(juce::Decibels::gainToDecibels(gain),1):juce::String("-inf");};
    meter.setText("INPUT  "+asDb(processor.inputPeak.load(std::memory_order_relaxed))+" dBFS     /     OUTPUT  "+asDb(processor.outputPeak.load(std::memory_order_relaxed))+" dBFS     /     block sample peaks",juce::dontSendNotification);
    // Host state can be restored while the editor is open. Do not overwrite active typing.
    if(!prompt.hasKeyboardFocus(true)){
        const auto saved=processor.parameters.state.getProperty("prompt").toString();
        if(prompt.getText()!=saved)prompt.setText(saved,false);
    }
    const int savedGenre=static_cast<int>(processor.parameters.state.getProperty("genre",0));
    if(genre.getSelectedItemIndex()!=savedGenre)genre.setSelectedItemIndex(savedGenre,juce::dontSendNotification);
    pollCapture();
    finishDirection();
}
