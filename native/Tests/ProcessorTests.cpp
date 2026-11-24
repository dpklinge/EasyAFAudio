#include "PluginProcessor.h"
#include "LanguagePlanner.h"
#include "LoudnessMeter.h"
#include <iostream>
#include <stdexcept>
#include <juce_audio_formats/juce_audio_formats.h>
static void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}

static void testMasteringMeters() {
    constexpr int rate = 48000;
    constexpr int frames = 480;
    std::array<float, frames * 2> audio {};
    const auto addTone = [&audio, frames](intent::LoudnessMeter& meter, float level, int seconds) {
        const auto gain = intent::Engine::dbToGain(level);
        for (int i = 0; i < frames; ++i) {
            const auto sample = gain * static_cast<float>(std::sin(6.283185307179586 * i / 48.0));
            audio[2 * i] = audio[2 * i + 1] = sample;
        }
        for (int block = 0; block < seconds * 100; ++block)
            meter.add(audio.data(), frames);
    };
    // Synthetic stereo references based on EBU Tech 3341, Table 1.
    intent::LoudnessMeter tone(rate, 2);
    addTone(tone, -23.0f, 20);
    const auto reference = tone.finish();
    require(reference.lufs && std::abs(*reference.lufs + 23.0f) <= 0.1f,
            "The stereo reference tone must read -23.0 LUFS");

    intent::LoudnessMeter gated(rate, 2);
    addTone(gated, -72.0f, 10);
    addTone(gated, -36.0f, 10);
    addTone(gated, -23.0f, 60);
    addTone(gated, -36.0f, 10);
    addTone(gated, -72.0f, 10);
    const auto gatedReading = gated.finish();
    require(gatedReading.lufs && std::abs(*gatedReading.lufs + 23.0f) <= 0.1f,
            "Quiet passages must be excluded by the loudness gates");

    intent::LoudnessMeter intersample(rate, 2);
    const auto quarterRatePeak = static_cast<float>(std::sqrt(0.125));
    for (int block = 0; block < 100; ++block) {
        for (int i = 0; i < frames; ++i) {
            const int position = block * frames + i;
            const auto fade = std::min({1.0f, position / 480.0f, (rate - 1 - position) / 480.0f});
            const auto sample = ((i % 4) < 2 ? quarterRatePeak : -quarterRatePeak) * fade;
            audio[2 * i] = audio[2 * i + 1] = sample;
        }
        intersample.add(audio.data(), frames);
    }
    const auto truePeak = intersample.finish().truePeakDb;
    require(truePeak && *truePeak >= -6.4f && *truePeak <= -5.8f,
            "The intersample test tone must measure around -6 dBTP while its samples peak near -9 dBFS");

    intent::LoudnessMeter shortMono(rate, 1);
    audio.fill(0.25f);
    shortMono.add(audio.data(), frames);
    const auto shortReading = shortMono.finish();
    require(!shortReading.lufs && shortReading.truePeakDb,
            "A scan under 400 ms must retain true peak and report unavailable integrated loudness");
    std::cout << "PASS: reference and gated LUFS, intersample true peak, and short capture handling\n";
}

static void testTruePeakLimiter() {
    for (const int rate : {44100, 48000, 96000, 192000}) {
        intent::Engine engine;
        engine.prepare(rate);
        intent::Settings settings;
        settings.guard = true;
        settings.ceilingDb = -1.0f;
        intent::LoudnessMeter inputMeter(rate, 2), outputMeter(rate, 2);
        constexpr int blockSize = 256;
        std::array<float, blockSize> left, right;
        std::array<float, blockSize * 2> measurement;
        float* channels[] = {left.data(), right.data()};
        bool reduced = false;
        const auto transfer = [&](intent::LoudnessMeter& meter) {
            for (int i = 0; i < blockSize; ++i) {
                measurement[2 * i] = left[i];
                measurement[2 * i + 1] = right[i];
            }
            meter.add(measurement.data(), blockSize);
        };
        for (int offset = 0; offset < rate; offset += blockSize) {
            for (int i = 0; i < blockSize; ++i) {
                const int sample = offset + i;
                const int burstFrame = sample % (rate / 8);
                const auto taper = std::clamp(std::min(burstFrame, rate / 16 - burstFrame) / 240.0f, 0.0f, 1.0f);
                const float highTone = sample % 4 >= 2 ? -0.80f : 0.80f;
                left[i] = highTone * taper;
                if (burstFrame == 13)
                    left[i] += 1.4f;
                right[i] = left[i] * 0.5f;
            }
            transfer(inputMeter);
            reduced |= engine.process(channels, 2, blockSize, settings);
            for (int i = 0; i < blockSize; ++i)
                require(std::abs(right[i] * 2.0f - left[i]) < 1e-6f, "Limiter must link stereo gain");
            transfer(outputMeter);
        }
        for (int remaining = engine.latencySamples(); remaining > 0; remaining -= blockSize) {
            left.fill(0.0f);
            right.fill(0.0f);
            engine.process(channels, 2, blockSize, settings);
            transfer(outputMeter);
        }
        const auto output = outputMeter.finish();
        require(reduced && output.truePeakDb && *output.truePeakDb <= -1.0f,
                "Limiter must contain reconstructed peaks and short bursts under the selected ceiling");
        const auto input = inputMeter.finish();
        require(input.truePeakDb && *input.truePeakDb > -1.0f, "The test input must exceed the ceiling");
        std::cout << rate << " Hz limiter: " << *input.truePeakDb << " -> " << *output.truePeakDb << " dBTP\n";
    }
}

struct MediaTrack {
    double gain = 1.0;
    double amplitude = 0.0;
    int effects = 0;
};

struct AudioAccessor { MediaTrack* track; };

struct ReaProject {
    double duration = 12.003;
    double cursor = 3.5;
    int repeat = 1;
    int state = 0;
    int revision = 0, undoCommits = 0;
    MediaTrack master;
    std::array<MediaTrack, 3> tracks {{{1.0, 0.8, 0}, {1.0, 0.1, 0}, {1.0, 0.9, 1}}};
};

static void testTrackBalance() {
    using Balance = intent::TrackBalance;
    using State = Balance::State;
    ReaProject project;
    Balance::Api api;
    api.hostTrack = &project.master;
    api.master = [](ReaProject* p) { return &p->master; };
    api.trackCount = [](ReaProject* p) { return static_cast<int>(p->tracks.size()); };
    api.track = [](ReaProject* p, int index) { return &p->tracks[static_cast<size_t>(index)]; };
    api.parent = [](MediaTrack*) -> MediaTrack* { return nullptr; };
    api.get = [](MediaTrack* t, const char* property) {
        const std::string_view name(property);
        return name == "B_MAINSEND" ? 1.0 : name == "D_VOL" ? t->gain : 0.0;
    };
    api.set = [](MediaTrack* t, const char*, double gain) { t->gain = gain; return true; };
    api.items = [](MediaTrack*) { return 1; };
    api.effects = [](MediaTrack* t) { return t->effects; };
    api.envelopes = [](MediaTrack*) { return 0; };
    api.sends = [](MediaTrack*, int) { return 0; };
    api.revision = [](ReaProject* p) { return p->revision; };
    api.access = [](MediaTrack* track) { return new AudioAccessor{track}; };
    api.close = [](AudioAccessor* accessor) { delete accessor; };
    api.begin = [](AudioAccessor*) { return 0.0; };
    api.end = [](AudioAccessor*) { return 4.0; };
    api.samples = [](AudioAccessor* accessor, int rate, int channels, double time, int count, double* audio) {
        for (int i = 0; i < count; ++i) {
            const auto sample = accessor->track->amplitude * std::sin((time + static_cast<double>(i) / rate) * 6283.185307179586);
            for (int channel = 0; channel < channels; ++channel)
                audio[i * channels + channel] = sample;
        }
        return 1;
    };
    api.beginUndo = [](ReaProject*) {};
    api.endUndo = [](ReaProject* p, const char*, int) { ++p->undoCommits; };
    api.refresh = [] {};
    Balance balance;
    const auto run = [&] {
        auto state = balance.start(api, &project);
        for (int step = 0; step < 500 && state == State::measuring; ++step)
            state = balance.step();
        return state;
    };
    require(run() == State::applied && balance.changedTracks() == 1, "Two eligible tracks must yield one measured cut");
    const auto target = project.tracks[0].gain;
    require(std::abs(target - std::pow(10.0, -6.0 / 20.0)) < 0.0001, "Loud track must use the bounded cut");
    require(project.tracks[1].gain == 1.0 && project.tracks[2].gain == 1.0 && project.undoCommits == 1,
            "Quiet and FX tracks must remain untouched, and changes must use one undo step");
    require(run() == State::alreadyBalanced && project.tracks[0].gain == target && project.undoCommits == 1,
            "Rerunning the same source must not accumulate fader cuts");
    project.tracks[0].gain = 1.0;
    require(balance.start(api, &project) == State::measuring, "The track scan must start");
    ++project.revision;
    require(balance.step() == State::interrupted && project.tracks[0].gain == 1.0,
            "An edited project must not receive stale balance changes");

    IntentProcessor processor;
    processor.transport.setApi({
        &project, [](ReaProject* p) { return p->duration; }, [](ReaProject* p) { return p->cursor; },
        [](ReaProject*) { return 1.0; }, [](ReaProject* p) { return p->state; },
        [](ReaProject*, int value) { return value; }, [](ReaProject*, double, bool, bool) {},
        [](ReaProject*) {}, [](ReaProject*) {}
    });
    processor.transport.setTrackApi(api);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    juce::TextButton* button = nullptr;
    for (auto* child : editor->getChildren())
        if (auto* candidate = dynamic_cast<juce::TextButton*>(child);
            candidate && candidate->getButtonText() == "Balance tracks")
            button = candidate;
    require(button && button->isVisible() && button->onClick, "REAPER master editor must expose Balance tracks");
    button->onClick();
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
    while (button->getButtonText() == "Cancel track balance" && juce::Time::getMillisecondCounterHiRes() < deadline) {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
    }
    require(button->getButtonText() == "Balance tracks" && project.tracks[0].gain == target,
            "One editor click must complete the separate track balance");
    std::cout << "PASS: one-click track measurements, bounded gain cuts, undo grouping, repeat, and stale-project rejection\n";
}

class TestPlayHead final : public juce::AudioPlayHead {
public:
    explicit TestPlayHead(const ReaProject& project) : project(project) {}
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setTimeInSamples(frame);
        info.setIsPlaying(project.state == 1);
        return info;
    }
    std::int64_t frame = 0;
private:
    const ReaProject& project;
};

static void feedTone(IntentProcessor& processor, TestPlayHead& playHead, int frames, float frequency,
                     float rightPhase = 0.0f, bool clippingFixture = false) {
    juce::AudioBuffer<float> buffer(2, 480);
    juce::MidiBuffer midi;
    for (int position = 0; position < frames; position += buffer.getNumSamples()) {
        playHead.frame = position;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const auto index = position + i;
            const auto hz = index < 48000 * 7 ? 1000.0f : frequency;
            const auto gain = index < 48000 * 7 ? 0.03f : 0.25f;
            const float phase = index * hz * 6.283185307f / 48000.0f;
            buffer.setSample(0, i, gain * std::sin(phase));
            buffer.setSample(1, i, gain * std::sin(phase + (index < 48000 * 7 ? 0.0f : rightPhase)));
            if (clippingFixture && index >= 48000 * 6 && index < 48000 * 6 + 4800)
                buffer.setSample(0, i, 0.4f * std::clamp(std::sin(phase), -0.45f, 0.45f));
            if (clippingFixture && index == 48000 * 8)
                buffer.setSample(1, i, 1.4f);
        }
        processor.processBlock(buffer, midi);
        if (position % 48000 == 0)
            juce::Thread::sleep(10);
    }
}

static void reportSong(const juce::String& path) {
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(
        juce::File::getCurrentWorkingDirectory().getChildFile(path)));
    require(reader && reader->numChannels == 2, "Expected stereo WAV for song benchmark");
    const int rate = static_cast<int>(reader->sampleRate);
    intent::FullScan capture;
    capture.prepare(rate);
    require(capture.start(static_cast<double>(reader->lengthInSamples) / rate), "Could not arm song capture");
    auto recording = std::async(std::launch::async, [&capture] { return capture.collect(); });
    juce::AudioBuffer<float> audio(2, 4096);
    for (std::int64_t offset = 0; offset < reader->lengthInSamples;) {
        const auto count = static_cast<int>(std::min<std::int64_t>(4096, reader->lengthInSamples - offset));
        if (!reader->read(&audio, 0, count, offset, true, true) || capture.failed()) {
            capture.cancel();
            throw std::runtime_error("Could not capture complete song test input");
        }
        capture.record(audio.getArrayOfReadPointers(), 2, count, offset);
        offset += count;
        juce::Thread::sleep(1);
    }
    if (!capture.complete())
        capture.cancel();
    const auto file = recording.get();
    require(file && file->frames == reader->lengthInSamples, "Scan must contain the complete test input");
    auto direction = intent::makeLanguagePlan(2, "warm low end and airy highs");
    direction.settings.guard = true;
    const auto adapted = capture.plan(*file, direction.settings);
    require(adapted.has_value(), "Song excerpt must be usable");
    std::cout << "Full test export, " << file->frames << " frames at " << file->sampleRate << " Hz (RMS, sample peak, dBFS)\n"
              << "Input " << adapted->before.rmsDb << ", " << adapted->before.peakDb << '\n'
              << "Analyze & Build " << adapted->after.rmsDb << ", " << adapted->after.peakDb << '\n'
              << "Adapted warmth, punch, width, air, output: " << adapted->settings.warmth << ", "
              << adapted->settings.punch << ", " << adapted->settings.width << ", " << adapted->settings.air
              << ", " << adapted->settings.outputDb << '\n';
    if (adapted->before.lufs && adapted->after.lufs && adapted->before.truePeakDb && adapted->after.truePeakDb)
        std::cout << "Integrated " << *adapted->before.lufs << " -> " << *adapted->after.lufs << " LUFS\n"
                  << "True peak " << *adapted->before.truePeakDb << " -> " << *adapted->after.truePeakDb << " dBTP\n";
}

int main(int argc,char** argv){try{
    juce::ScopedJuceInitialiser_GUI init;
    testMasteringMeters();
    testTruePeakLimiter();
    testTrackBalance();
#if INTENT_USE_MINILM
    const auto semantic = intent::makeLanguagePlan(0, "Give the sub bass more heft and stretch the stereo field");
    std::cout << semantic.explanation << '\n';
    require(semantic.settings.warmth > 50 && semantic.settings.width > 65,
            "MiniLM must recognize two unfamiliar sound directions");
    const auto mixed = intent::makeLanguagePlan(0, "warm, let the transients come through");
    std::cout << mixed.explanation << '\n';
    require(mixed.settings.warmth == 65 && mixed.settings.punch < 65,
            "MiniLM must add a semantic change alongside an exact keyword");
    const auto narrower = intent::makeLanguagePlan(0, "reduce stereo width");
    require(narrower.settings.width < 65, "A request to reduce width must narrow the mix");
    const auto treble = intent::makeLanguagePlan(0, "cut the brittle high end");
    require(treble.settings.air < 60, "A request to reduce treble must lower Air");
    const auto scope = intent::makeLanguagePlan(0, "raise the lead vocal level");
    require(scope.settings.warmth == 50 && scope.settings.punch == 65
            && scope.settings.width == 65 && scope.settings.air == 60,
            "An unsupported request must retain the baseline");
    const auto negation = intent::makeLanguagePlan(0, "do not brighten it");
    require(negation.negated && negation.settings.air == 60, "Negation must retain the baseline");
#endif
    IntentProcessor source;
    source.prepareToPlay(48000,256);
    source.applyDirection(1,"warm punchy intimate");
    auto* output=source.parameters.getParameter("output");output->setValueNotifyingHost(output->convertTo0to1(-3));
    juce::MemoryBlock saved;source.getStateInformation(saved);
    IntentProcessor restored;restored.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    for(const auto* id:{"warmth","punch","width","air","output","ceiling","guard","bypass"})
        require(std::abs(source.parameters.getRawParameterValue(id)->load()-restored.parameters.getRawParameterValue(id)->load())<1e-5f,"Parameter state must round trip");
    require(restored.parameters.state.getProperty("prompt").toString()=="warm punchy intimate","Prompt must round trip");
    require(static_cast<int>(restored.parameters.state.getProperty("genre"))==1,"Genre must round trip");
    const float previous=restored.parameters.getRawParameterValue("warmth")->load();
    const char invalid[]="not a preset";restored.setStateInformation(invalid,sizeof(invalid));
    require(restored.parameters.getRawParameterValue("warmth")->load()==previous,"Corrupt state must not change parameters");
    auto damaged=source.parameters.copyState();
    damaged.getChild(0).setProperty("value",99999,nullptr);
    juce::MemoryBlock corrupt;juce::AudioProcessor::copyXmlToBinary(*damaged.createXml(),corrupt);
    restored.setStateInformation(corrupt.getData(),static_cast<int>(corrupt.getSize()));
    require(restored.parameters.getRawParameterValue("warmth")->load()==previous,"Out of range state must be rejected");
    restored.prepareToPlay(48000,256);
    require(restored.getLatencySamples() == 253, "Report the same look-ahead delay to the DAW");
    ReaProject testProject;
    TestPlayHead testPlayHead(testProject);
    restored.setPlayHead(&testPlayHead);
    restored.transport.setApi({
        &testProject, [](ReaProject* p) { return p->duration; }, [](ReaProject* p) { return p->cursor; },
        [](ReaProject*) { return 1.0; }, [](ReaProject* p) { return p->state; },
        [](ReaProject* p, int repeat) {
            if (repeat != -1) p->repeat = repeat;
            return p->repeat;
        },
        [](ReaProject* p, double position, bool, bool) { p->cursor = position; },
        [](ReaProject* p) { p->state = 1; }, [](ReaProject* p) { p->state = 0; }
    });
    juce::AudioBuffer<float> buffer(2,256);juce::MidiBuffer midi;
    for(int block=0;block<100;++block){for(int c=0;c<2;++c)for(int i=0;i<256;++i)buffer.setSample(c,i,0.3f*std::sin((block*256+i)*0.1f));restored.processBlock(buffer,midi);}
    require(std::isfinite(buffer.getMagnitude(0,0,256)),"Processor output must be finite");
    require(restored.outputPeak.load()>0,"Output meter must update");
    { std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());require(editor!=nullptr,"Editor must construct");require(editor->getWidth()==880,"Editor must have designed width");
      juce::ComboBox* genreSelector=nullptr;
      juce::TextButton* resetButton=nullptr;
      juce::TextButton* analyzeButton=nullptr;
      juce::Label* scanResult=nullptr;
#if INTENT_USE_MINILM
      juce::TextButton* buildButton=nullptr;
      juce::TextEditor* promptEditor=nullptr;
#endif
      for(auto* child:editor->getChildren()){
        if(auto* combo=dynamic_cast<juce::ComboBox*>(child); combo && combo->getTitle()=="Genre")
          genreSelector=combo;
        if(auto* button=dynamic_cast<juce::TextButton*>(child); button && button->getButtonText()=="Reset to genre defaults")
          resetButton=button;
        if(auto* button=dynamic_cast<juce::TextButton*>(child); button && button->getButtonText()=="Analyze & Build")
          analyzeButton=button;
#if INTENT_USE_MINILM
        if(auto* button=dynamic_cast<juce::TextButton*>(child); button && button->getButtonText()=="Build direction")
          buildButton=button;
        if(auto* field=dynamic_cast<juce::TextEditor*>(child); field && field->getTitle()=="Sound direction")
          promptEditor=field;
#endif
      }
      require(genreSelector && resetButton && resetButton->isVisible() && resetButton->onClick,
              "Editor must expose a working genre reset");
      constexpr const char* ids[]={"warmth","punch","width","air","output","ceiling","guard","bypass"};
      constexpr float expected[][8]={
        {50,65,65,60,-1,-1,0,0}, {65,70,45,45,-1.5f,-1,0,0}, {45,60,60,65,-1,-1,0,0},
        {55,65,45,50,-0.5f,-1,0,0}, {55,30,45,45,0,-1.5f,0,0}, {60,20,80,65,-1,-1.5f,0,0},
      };
      for(int index=0;index<6;++index){
        for(const auto* id:ids)restored.parameters.getParameter(id)->setValueNotifyingHost(1);
        genreSelector->setSelectedItemIndex(index,juce::sendNotificationSync);
        resetButton->onClick();
        for(int parameter=0;parameter<8;++parameter){
          const auto actual=restored.parameters.getRawParameterValue(ids[parameter])->load();
          require(std::abs(actual-expected[index][parameter])<1e-5f,
                  "Reset button must restore all eight controls for the selected genre");
        }
        require(restored.parameters.state.getProperty("prompt").toString()=="warm punchy intimate",
                "Reset must preserve prompt text");
      }
      restored.applyDirection(5,"warm");
      require(restored.parameters.getRawParameterValue("warmth")->load()==75,
              "Build direction must still apply keywords after reset");
      require(std::abs(restored.parameters.getRawParameterValue("output")->load()+1)<1e-5f,
              "Build direction must preserve output trim after reset");
#if INTENT_USE_MINILM
      require(buildButton && buildButton->onClick && promptEditor, "Editor must expose Build direction");
      const juce::String phrase = "make the drum hits more forceful";
      genreSelector->setSelectedItemIndex(0, juce::sendNotificationSync);
      resetButton->onClick();
      promptEditor->setText(phrase, false);
      restored.parameters.state.setProperty("prompt", phrase, nullptr);
      buildButton->onClick();
      require(!buildButton->isEnabled(), "Build direction must start a background request");
      const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
      while (!buildButton->isEnabled() && juce::Time::getMillisecondCounterHiRes() < deadline) {
        juce::Thread::sleep(10);
        juce::Timer::callPendingTimersSynchronously();
      }
      require(buildButton->isEnabled(), "Editor must receive the background result");
      require(restored.parameters.getRawParameterValue("punch")->load() > 65,
              "Build direction must apply the MiniLM result to the live parameter");
#endif
      require(analyzeButton && analyzeButton->onClick, "Editor must expose Analyze & Build");
      auto waitForAnalysis = [&] {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 6000;
        do {
          juce::Thread::sleep(10);
          juce::Timer::callPendingTimersSynchronously();
        } while ((!analyzeButton->isEnabled() || analyzeButton->getButtonText()=="Cancel analysis")
                  && juce::Time::getMillisecondCounterHiRes()<deadline);
        require(analyzeButton->isEnabled() && analyzeButton->getButtonText()=="Analyze & Build",
                "Full scan must return control to the editor");
        require(testProject.state == 0 && testProject.cursor == 3.5 && testProject.repeat == 1,
                "Scan must stop its playback and restore cursor and repeat");
      };
      auto scan = [&](float frequency, float rightPhase, bool clippingFixture = false) {
        restored.resetToGenreDefaults(genreSelector->getSelectedItemIndex());
        analyzeButton->onClick();
        require(analyzeButton->getButtonText()=="Cancel analysis", "Capture must arm");
        require(testProject.state == 1 && testProject.cursor == 0 && testProject.repeat == 0,
                "The button must start the scan at project zero without looping");
        feedTone(restored, testPlayHead, static_cast<int>(std::ceil(48000 * testProject.duration)),
                 frequency, rightPhase, clippingFixture);
        waitForAnalysis();
        scanResult = nullptr;
        for (auto* child : editor->getChildren())
          if (auto* label = dynamic_cast<juce::Label*>(child); label && label->getText().startsWith("Full scan"))
            scanResult = label;
        require(scanResult && scanResult->getText().contains("Sample peak"), "Analysis must report actual measurements");
        return restored.currentSettings();
      };
      const auto low = scan(60, 0);
      const auto high = scan(9000, 3.14159265f);
      require(low.warmth + 15 < high.warmth && high.air + 15 < low.air,
              "The different material after the first seven seconds must affect the result");
      require(high.width <= 45, "Anti-correlated audio must suppress widening");
      analyzeButton->onClick();
      analyzeButton->onClick();
      waitForAnalysis();
      analyzeButton->onClick();
      feedTone(restored, testPlayHead, 480, 60);
      testPlayHead.frame = 960;
      restored.processBlock(buffer, midi);
      waitForAnalysis();
      const auto afterSeek = restored.currentSettings();
      require(afterSeek.warmth == high.warmth && afterSeek.air == high.air,
              "A partial scan containing a seek must leave settings unchanged");
      auto hasLabel = [&](const juce::String& value) {
        for (auto* child : editor->getChildren())
          if (auto* label = dynamic_cast<juce::Label*>(child); label && label->getText().contains(value))
            return true;
        return false;
      };
      scan(9000, 0);
      require(!hasLabel("Possible input clipping") && !hasLabel("Input exceeded"),
              "Clean input must not report clipping");
      scan(9000, 0, true);
      require(hasLabel("LUFS") && hasLabel("dBTP"), "Full scan must show integrated loudness and true peak");
      require(restored.currentSettings().guard, "Analyze & Build must enable the true-peak limiter");
      require(hasLabel("Possible input clipping at 0:06.")
              && hasLabel("Input also exceeded 0 dBFS at 0:08.000"),
              "Scan must timestamp flattened peaks below full scale separately from overload");
      if(argc>1 && juce::String(argv[1])!="--song"){
        juce::FileOutputStream file(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
        require(file.openedOk(),"Snapshot output must open");
        require(file.setPosition(0),"Snapshot stream must rewind");
        require(file.truncate().wasOk(),"Previous snapshot must be truncated");
        juce::PNGImageFormat png;
        require(png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),file),"Editor snapshot must render");
      }
    }
    restored.setPlayHead(nullptr);
    if(argc==3 && juce::String(argv[1])=="--song")
        reportSong(argv[2]);
    restored.releaseResources();source.releaseResources();
    std::cout<<"PASS: state round trip, corrupt state rejection, audio, meters, editor, and all six genre resets\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
