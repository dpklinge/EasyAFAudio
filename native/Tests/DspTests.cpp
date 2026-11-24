#include "IntentDsp.h"
#include "KeywordPlanner.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool pass,const char* what){if(!pass)throw std::runtime_error(what);}
int main(){try{
    constexpr int n=1024;
    std::array<float,n> left{},right{};float* channels[]={left.data(),right.data()};
    intent::Engine e;intent::Settings s;
    for(double rate:{8000.0,44100.0,48000.0,96000.0,192000.0}){
        e.prepare(rate);
        for(int i=0;i<n;++i){left[i]=std::sin(i*0.1f)*0.4f;right[i]=std::cos(i*0.07f)*0.3f;}
        const auto a=left,b=right;e.process(channels,2,n,s);
        const auto latency=e.latencySamples();
        for(int i=0;i<n;++i){
            const auto index=i-latency;
            require(std::abs(left[i]-(index<0?0.0f:a[index]))<1e-6f,"Neutral left must preserve the delayed signal");
            require(std::abs(right[i]-(index<0?0.0f:b[index]))<1e-6f,"Neutral right must preserve the delayed signal");
        }
    }
    // Compressor must reduce sustained loud material; stereo detector is linked.
    e.prepare(48000);s.punch=100;
    for(int block=0;block<200;++block){left.fill(0.8f);right.fill(0.4f);e.process(channels,2,n,s);}
    require(left.back()<0.4f,"Compression should reduce sustained level");
    require(std::abs(left.back()/right.back()-2)<0.001f,"Compressor stereo link must preserve balance");
    // Bypass settles to unity even after substantial processing.
    s.bypass=true;s.warmth=100;s.air=100;s.outputDb=12;
    for(int block=0;block<100;++block){left.fill(0.1f);right.fill(0.2f);e.process(channels,2,n,s);}
    require(std::abs(left.back()-0.1f)<1e-5f,"Bypass must return dry audio");
    // Limiter bounds samples and reports when gain reduction is audible.
    e.prepare(48000);s={};s.guard=true;s.outputDb=12;s.ceilingDb=-1;
    left.fill(1);right.fill(-1);
    require(e.process(channels,2,n,s),"Limiter must report gain reduction");
    for(auto sample:left)require(std::abs(sample)<=intent::Engine::dbToGain(-1)+1e-6f,"Limiter must contain samples");
    left.fill(0.18f);right.fill(-0.18f);s.outputDb=0;e.prepare(48000);
    require(!e.process(channels,2,n,s),"A quiet flat top must not trigger the limiter");
    require(std::abs(left.back()-0.18f)<1e-6f,"The limiter must leave a below-ceiling plateau alone");
    s.bypass=true;
    for(int block=0;block<100;++block){left.fill(1.2f);right.fill(-1.2f);e.process(channels,2,n,s);}
    s.bypass=false;left.fill(1.2f);right.fill(-1.2f);
    require(e.process(channels,2,n,s),"Limiter must report peaks as processing resumes from bypass");
    for(auto sample:left)require(std::abs(sample)<=intent::Engine::dbToGain(-1)+1e-6f,
                                "Limiter must protect the output during the transition out of bypass");
    // Invalid input and settings cannot poison internal state.
    e.prepare(48000);s={};s.warmth=std::numeric_limits<float>::quiet_NaN();
    left.fill(std::numeric_limits<float>::infinity());right.fill(std::numeric_limits<float>::quiet_NaN());
    e.process(channels,2,n,s);for(auto sample:left)require(std::isfinite(sample),"Output must remain finite");
    left.fill(0.2f);e.process(channels,1,n,s);
    for(int i=e.latencySamples();i<n;++i)require(std::abs(left[i]-0.2f)<1e-5f,"Mono after invalid input must recover");
    // Block partitioning must not alter DSP results.
    intent::Engine a,b;a.prepare(48000);b.prepare(48000);s={};s.warmth=90;s.punch=70;s.air=85;s.guard=true;s.outputDb=10;
    std::array<float,n> x{},y{};for(int i=0;i<n;++i)x[i]=y[i]=0.7f*std::sin(i*0.2f);
    float* ap[]={x.data()};a.process(ap,1,n,s);
    for(int offset=0;offset<n;offset+=64){float* bp[]={y.data()+offset};b.process(bp,1,64,s);}
    for(int i=0;i<n;++i)require(std::abs(x[i]-y[i])<1e-6f,"DSP must be block-size independent");
    const auto base=intent::makePlan(0,"");
    require(intent::makePlan(0,"warm").settings.warmth>base.settings.warmth,"Warm keyword must affect warmth");
    require(intent::makePlan(0,"swarm").settings.warmth==base.settings.warmth,"No substring keywords");
    require(intent::makePlan(0,"not bright").settings.air==base.settings.air,"Negation must not apply bright");
    require(intent::makePlan(0,"bright dark").explanation.find("conflict")!=std::string::npos,"Conflict must be reported");
    std::cout<<"PASS: neutral signal, 5 sample rates, compression, stereo link, bypass, guard, invalid inputs, mono, block partitioning, keyword planning\n";
    return EXIT_SUCCESS;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return EXIT_FAILURE;}}
