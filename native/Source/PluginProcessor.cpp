#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KeywordPlanner.h"
#include <charconv>

juce::AudioProcessorValueTreeState::ParameterLayout IntentProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const char* ids[] = {"warmth","punch","width","air"};
    const char* names[] = {"Warmth","Punch","Width","Air"};
    for (int i=0;i<4;++i)
        result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids[i],1},names[i],0.0f,100.0f,i==1?0.0f:50.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output (dB)",-24.0f,12.0f,0.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"ceiling",1},"Sample peak ceiling (dBFS)",-3.0f,-0.1f,-1.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"guard",1},"Sample peak guard",false));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
    return result;
}
IntentProcessor::IntentProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                      .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"IntentState",layout()) {
    const char* ids[]={"warmth","punch","width","air","output","ceiling","guard","bypass"};
    for (size_t i=0;i<raw.size();++i) raw[i]=parameters.getRawParameterValue(ids[i]);
    parameters.state.setProperty("schemaVersion",1,nullptr);
    parameters.state.setProperty("genre",0,nullptr);
    parameters.state.setProperty("prompt","Warm low end, punchy drums, wide and airy",nullptr);
}
void IntentProcessor::prepareToPlay(double rate,int) { engine.prepare(rate); capture.prepare(rate); setLatencySamples(0); }
bool IntentProcessor::isBusesLayoutSupported(const BusesLayout& b) const {
    const auto out=b.getMainOutputChannelSet();
    return (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo()) && out==b.getMainInputChannelSet();
}
void IntentProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi) { midi.clear(); process(b,false); }
void IntentProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer& midi) { midi.clear(); process(b,true); }
void IntentProcessor::process(juce::AudioBuffer<float>& b,bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    for (int c=getTotalNumInputChannels();c<b.getNumChannels();++c) b.clear(c,0,b.getNumSamples());
    const int count=std::min({getTotalNumInputChannels(),b.getNumChannels(),2});
    if (count<1 || b.getNumSamples()==0) return;
    float inPeak=0;
    for (int c=0;c<count;++c) for(int i=0;i<b.getNumSamples();++i) {
        float& value=b.getWritePointer(c)[i];
        if(!std::isfinite(value))value=0;
        inPeak=std::max(inPeak,std::abs(value));
    }
    if (capture.wantsAudio() && !isNonRealtime())
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition(); position && position->getIsPlaying() && !position->getIsRecording())
                if (const auto frame = position->getTimeInSamples())
                    capture.record(b.getArrayOfReadPointers(), count, b.getNumSamples(), *frame);
    auto s = currentSettings();
    s.bypass = forceBypass || s.bypass;
    engine.process(b.getArrayOfWritePointers(),count,b.getNumSamples(),s);
    float outPeak=0;
    for(int c=0;c<count;++c)outPeak=std::max(outPeak,b.getMagnitude(c,0,b.getNumSamples()));
    inputPeak.store(inPeak,std::memory_order_relaxed); outputPeak.store(outPeak,std::memory_order_relaxed);
}
intent::Settings IntentProcessor::currentSettings() const noexcept {
    return {raw[0]->load(), raw[1]->load(), raw[2]->load(), raw[3]->load(),
            raw[4]->load(), raw[5]->load(), raw[6]->load() > 0.5f, raw[7]->load() > 0.5f};
}
juce::AudioProcessorParameter* IntentProcessor::getBypassParameter() const { return parameters.getParameter("bypass"); }
juce::AudioProcessorEditor* IntentProcessor::createEditor() { return new IntentEditor(*this); }
void IntentProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if(auto xml=parameters.copyState().createXml())copyXmlToBinary(*xml,destination);
}
void IntentProcessor::setStateInformation(const void* data,int size) {
    if(size<=0 || size>1024*1024)return;
    if(auto xml=getXmlFromBinary(data,size)) {
        auto restored=juce::ValueTree::fromXml(*xml);
        if(!restored.hasType("IntentState") || static_cast<int>(restored.getProperty("schemaVersion",0))!=1)return;
        // Validate the entire parameter payload before replacing the live tree.
        constexpr const char* ids[]={"warmth","punch","width","air","output","ceiling","guard","bypass"};
        for(const auto* id:ids){
            int matches=0;
            for(const auto& child:restored)if(child.getProperty("id").toString()==id){
                ++matches;
                if(!child.hasType("PARAM"))return;
                // ValueTree XML restores attributes as strings, including numeric values.
                const auto encoded=child.getProperty("value").toString().toStdString();
                float value=0;
                const auto parsed=std::from_chars(encoded.data(),encoded.data()+encoded.size(),value);
                if(parsed.ec!=std::errc{}||parsed.ptr!=encoded.data()+encoded.size())return;
                const auto range=parameters.getParameterRange(id);
                if(!std::isfinite(value)||value<range.start||value>range.end)return;
            }
            if(matches!=1)return;
        }
        if(restored.getNumChildren()!=8)return;
        const int genre=static_cast<int>(restored.getProperty("genre",0));
        if(genre<0||genre>5||restored.getProperty("prompt").toString().length()>500)return;
        parameters.replaceState(restored);
        ++stateRestoreRevision;
    }
}
juce::String IntentProcessor::applyDirection(int genre,const juce::String& prompt) {
    const auto safePrompt=prompt.substring(0,500);
    const auto plan=intent::makePlan(genre,safePrompt.toStdString());
    applyDirectionPlan(genre, safePrompt, plan);
    return juce::String(plan.explanation);
}

void IntentProcessor::applyDirectionPlan(int genre, const juce::String& prompt, const intent::Plan& plan) {
    applySettings(plan.settings, 4);
    parameters.state.setProperty("genre",intent::genreIndex(genre),nullptr);
    parameters.state.setProperty("prompt",prompt.substring(0, 500),nullptr);
}
void IntentProcessor::applyAudioPlan(int genre, const juce::String& prompt, const intent::AudioPlan& plan) {
    applySettings(plan.settings, 5);
    parameters.state.setProperty("genre", intent::genreIndex(genre), nullptr);
    parameters.state.setProperty("prompt", prompt.substring(0, 500), nullptr);
}

void IntentProcessor::resetToGenreDefaults(int genre) {
    const auto index = intent::genreIndex(genre);
    parameters.state.setProperty("genre", index, nullptr);
    applySettings(intent::genreDefaults[index], 8);
}

void IntentProcessor::applySettings(const intent::Settings& settings, int controlCount) {
    constexpr const char* ids[] = {"warmth", "punch", "width", "air", "output", "ceiling", "guard", "bypass"};
    const float values[] = {settings.warmth, settings.punch, settings.width, settings.air,
                            settings.outputDb, settings.ceilingDb,
                            static_cast<float>(settings.guard), static_cast<float>(settings.bypass)};
    for (int i = 0; i < controlCount; ++i) {
        auto* parameter = parameters.getParameter(ids[i]);
        const auto value = parameter->convertTo0to1(values[i]);
        if (juce::approximatelyEqual(parameter->getValue(), value))
            continue;

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(value);
        parameter->endChangeGesture();
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new IntentProcessor(); }
