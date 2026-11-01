#include "config/profile.h"
#include "core/planner.h"
#include "core/engine_math.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace aster;
namespace {
void require(bool value){if(!value)throw std::runtime_error("requirement failed");}
void near(double actual,double expected,double tolerance=1e-6){require(std::abs(actual-expected)<=tolerance);}
template<class F> void rejects(F run){bool rejected=false;try{run();}catch(const std::exception&){rejected=true;}require(rejected);}
Profile parse(const Json& j){std::vector<std::string> warnings;return profileFromJson(j,warnings);}
InputGate ready(){return {true,true,true,true,true};}
bool has(const std::vector<OutputEvent>& events,OutputKind kind,int code){
    return std::any_of(events.begin(),events.end(),[&](const auto& event){return event.kind==kind&&event.code==code;});
}
}
int main(){
    int passed=0,failed=0;
    const auto test=[&](const char* name,const std::function<void()>& run){
        try{run();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    test("Every module starts disabled and every setting/profile round-trips",[]{
        auto p=defaultProfile();
        for(const auto& d:moduleDefinitions()){
            auto& m=p.modules.at(d.id);require(!m.enabled);m.enabled=true;
            for(auto& [name,values]:m.profiles){
                for(const auto& s:d.settings)values[s.id]=s.kind==SettingKind::Boolean?Value{true}:Value{s.maximum};
                m.selectedProfile=name;
            }
        }
        p.bindings["timer"]={'Y',2};p.ui.scale=1.4f;
        std::vector<std::string> warnings;
        require(profileToJson(profileFromJson(profileToJson(p),warnings))==profileToJson(p));require(warnings.empty());
    });
    test("Legacy profiles migrate and module numbers clamp",[]{
        auto p=parse({{"schemaVersion",1},{"ui",{{"accentRGB",{0,210,255}}}},
            {"modules",{{"timer",{{"enabled",true},{"multiplier",99}}}}},{"keybinds",{{"auto360","TAB"}}}});
        require(p.schemaVersion==kSchemaVersion);require(p.modules.at("timer").enabled);near(p.modules.at("timer").number("multiplier"),5);
        near(p.ui.accent[2],1);require(p.bindings.at("auto360").key==9);
    });
    test("Schemas reject fractions, overflow and future versions",[]{
        for(const auto& value:Json::array({1.5,4294967299ULL,-4294967293LL,99,0}))
            rejects([&]{parse({{"schemaVersion",value}});});
        require(parse(Json::object()).schemaVersion==kSchemaVersion);
    });
    test("Integer key and navigation fields cannot wrap or truncate",[]{
        for(const auto& value:Json::array({87.5,4294967383ULL,-4294967209LL})){
            auto j=profileToJson(defaultProfile());j["movement"][0]=value;rejects([&]{parse(j);});
            j=profileToJson(defaultProfile());j["bindings"]["timer"]["key"]=value;rejects([&]{parse(j);});
            j=profileToJson(defaultProfile());j["ui"]["category"]=value;rejects([&]{parse(j);});
        }
        auto j=profileToJson(defaultProfile());j["bindings"]["timer"]["modifiers"]=-1;rejects([&]{parse(j);});
    });
    test("Fixed arrays enforce exact sizes and reject unrepresentable colors",[]{
        auto j=profileToJson(defaultProfile());j["movement"].push_back(88);rejects([&]{parse(j);});
        j=profileToJson(defaultProfile());j["ui"]["accent"]={0,0,1};rejects([&]{parse(j);});
        j=profileToJson(defaultProfile());j["ui"]["scale"]=1e100;rejects([&]{parse(j);});
    });
    test("Malformed configuration sections never silently discard their contents",[]{
        for(const auto& section:{"license","updates","bindings","movementKeys","keybinds"}){
            auto j=profileToJson(defaultProfile());if(std::string(section)=="keybinds")j.erase("bindings");
            j[section]=Json::array();rejects([&]{parse(j);});
        }
        auto j=profileToJson(defaultProfile());j["modules"]["timer"]["profiles"]=false;rejects([&]{parse(j);});
        j=profileToJson(defaultProfile());j["bindings"]["timer"]=42;rejects([&]{parse(j);});
    });
    test("Wrong setting types use defaults, invalid hotkeys are unbound",[]{
        auto j=profileToJson(defaultProfile());j["modules"]["clicker"]["profiles"]["default"]["randomize"]="yes";
        j["bindings"]["timer"]["key"]=0x78;std::vector<std::string> warnings;auto p=profileFromJson(j,warnings);
        require(!p.modules.at("clicker").flag("randomize"));require(p.bindings.at("timer").key==0);require(warnings.size()>=2);
        p.modules.at("timer").number("multiplier",std::numeric_limits<double>::quiet_NaN());
        require(!validate(p).empty());near(p.modules.at("timer").number("multiplier"),1.06);
    });
    test("Telemetry expires at 500 ms and cannot trust a future clock value",[]{
        for(auto now:{1499ULL,1500ULL,999ULL}){
            EngineRuntime r;r.bound=r.worldReady=r.permitted=r.blockTarget=r.entityTarget=true;
            for(auto& m:r.modules)m.conditions=m.applied=true;
            refreshEngineLiveness(r,now,1000);
            require(r.worldReady==(now==1499));require(r.permitted==(now==1499));require(r.modules[0].applied==(now==1499));
            if(now!=1499)require(!r.entityTarget&&!r.blockTarget);
        }
        EngineRuntime r;r.worldReady=r.permitted=true;refreshEngineLiveness(r,0,0);require(!r.worldReady&&!r.permitted);
    });
    test("Observed health requires the exact build and a running gate",[]{
        auto p=defaultProfile();p.modules.at("timer").enabled=true;GameSnapshot g;g.present=g.identityVerified=g.x64=g.foreground=true;g.build=std::string(kInputResearchBuild);
        RuntimeStatus r;r.armed=r.engine.bound=r.engine.worldReady=r.engine.permitted=true;
        auto& m=r.engine.modules[moduleIndex("timer")];m.bound=m.applied=m.conditions=true;m.observations=1;
        require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Observed);
        refreshEngineLiveness(r.engine,2000,1000);require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Bound);
        g.build="unsupported";require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Unsupported);
    });
    test("Remapping forward during S-Tap restores the key actually suppressed",[]{
        auto p=defaultProfile();p.modules.at("stap").enabled=true;InputPlanner planner;PhysicalInput physical;physical.keys['W']=true;InputContext context;
        planner.tick(0,p,physical,ready(),context);context.attackSequence=1;
        require(has(planner.tick(.01,p,physical,ready(),context),OutputKind::KeyUp,'W'));
        p.movement[0]='E';auto events=planner.tick(.02,p,physical,ready(),context);
        require(has(events,OutputKind::KeyDown,'W'));require(has(events,OutputKind::KeyUp,'S'));require(!has(events,OutputKind::KeyDown,'E'));
        require(planner.tick(.1,p,physical,ready(),context).empty());
    });
    test("Cancellation releases owned keys and respects held physical keys",[]{
        for(int i=0;i<5;++i){
            auto p=defaultProfile();p.modules.at("walk").enabled=true;InputPlanner planner;PhysicalInput physical;auto gate=ready();
            require(has(planner.tick(0,p,physical,gate),OutputKind::KeyDown,'W'));
            if(i==0)gate.armed=false;
            if(i==1)gate.gameVerified=false;
            if(i==2)gate.foreground=false;
            if(i==3)gate.cursorReady=false;
            if(i==4)gate.licensed=false;
            require(has(planner.tick(.01,p,physical,gate),OutputKind::KeyUp,'W'));require(planner.tick(.02,p,physical,gate).empty());
        }
        auto p=defaultProfile();p.modules.at("walk").enabled=true;InputPlanner planner;PhysicalInput physical;
        planner.tick(0,p,physical,ready());physical.keys['W']=true;require(planner.stop(physical).empty());
    });
    test("Canceling a planned batch retains pending releases of prior owned keys",[]{
        auto p=defaultProfile();p.modules.at("stap").enabled=true;InputPlanner planner;PhysicalInput physical;physical.keys['W']=true;InputContext context;
        planner.tick(0,p,physical,ready(),context);context.attackSequence=1;planner.tick(.01,p,physical,ready(),context);
        auto batch=planner.tick(.08,p,physical,ready(),context);
        require(!planner.ownsKey('S'));require(has(batch,OutputKind::KeyDown,'W'));require(has(batch,OutputKind::KeyUp,'S'));
        auto cleanup=planner.cancelBatch(batch,physical);require(has(cleanup,OutputKind::KeyUp,'S'));require(!has(cleanup,OutputKind::KeyDown,'W'));
        physical.keys['S']=true;require(!has(planner.cancelBatch(batch,physical),OutputKind::KeyUp,'S'));
    });
    test("Independent CPS streams preserve their average without catch-up bursts",[]{
        auto p=defaultProfile();p.modules.at("clicker").enabled=true;InputPlanner planner;PhysicalInput physical;planner.tick(0,p,physical,ready());physical.mouse={true,true};physical.pressSequence={1,2};
        std::array<int,2> counts{};
        for(int i=1;i<=20000;++i)for(const auto& event:planner.tick(i*.0005,p,physical,ready()))if(event.kind==OutputKind::Click)++counts[event.code];
        require(std::abs(counts[0]-130)<=2);require(std::abs(counts[1]-300)<=4);
        require(planner.tick(100,p,physical,ready()).size()<=2);require(planner.tick(100.001,p,physical,ready()).empty());
        auto gate=ready();gate.foreground=false;planner.tick(101,p,physical,gate);
        require(planner.tick(102,p,physical,ready()).empty());
    });
    test("Priming and inverted mining retain independent physical streams",[]{
        auto p=defaultProfile();auto& m=p.modules.at("clicker");m.enabled=true;m.flag("rightPriming",true);InputPlanner planner;PhysicalInput physical;
        planner.tick(0,p,physical,ready());physical.mouse[1]=true;physical.pressSequence[1]=1;
        planner.tick(.01,p,physical,ready());require(planner.tick(.2,p,physical,ready()).empty());
        physical.mouse[0]=true;physical.pressSequence[0]=2;planner.tick(.21,p,physical,ready());require(has(planner.tick(.3,p,physical,ready()),OutputKind::Click,1));
        planner.reset();physical={};m.flag("rightPriming",false);m.flag("invert",true);m.flag("breakAuto",true);InputContext context;context.blockTarget=true;
        planner.tick(0,p,physical,ready(),context);physical.mouse[1]=true;physical.pressSequence[1]=1;
        require(has(planner.tick(.01,p,physical,ready(),context),OutputKind::MouseDown,0));require(has(planner.stop(physical),OutputKind::MouseUp,0));
    });
    test("Triggerbot stops on world loss and non-finite scheduler clocks",[]{
        auto p=defaultProfile();p.modules.at("clicker").enabled=true;p.modules.at("clicker").flag("triggerbot",true);InputPlanner planner;PhysicalInput physical;InputContext context;
        planner.tick(0,p,physical,ready(),context);context.entityTarget=true;require(has(planner.tick(.1,p,physical,ready(),context),OutputKind::Click,0));
        context.worldReady=false;require(planner.tick(.2,p,physical,ready(),context).empty());
        require(planner.tick(std::numeric_limits<double>::quiet_NaN(),p,physical,ready()).empty());
    });
    test("Geometry and orientation preserve bounds and wrap correctly",[]{
        Box box{{-.3,-1,5},{.3,1,5.6}};near(*rayBox({},{0,0,1},box,5.2),5);require(!rayBox({},{0,0,1},box,4.99));
        near(*rayBox({1,0,0},{0,0,1},targetingBox(box,2.22),10),4.19);
        require(!rayBox({},{0,0,1},{{1,1,1},{-1,-1,-1}},10));near(wrappedDegrees(359),-1);near(lookAt({},{0,5,0}).pitch,-90);
        const auto corrected=aimCorrection({179,17},{-170,-30},25,false,.016,true,false,.02);require(corrected.yaw!=179);near(corrected.pitch,17);
        const auto impulse=scaledImpulse({.2,.1,0},{1,.5,-1},.5,.25,false,false,0);near(impulse.x,.6);near(impulse.y,.2);near(impulse.z,-.5);
        near(timerTps(1.06),21.2);near(timerBoost(.8),-20);
    });
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
