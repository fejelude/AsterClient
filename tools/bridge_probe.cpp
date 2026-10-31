#include "auth/license.h"
#include "integration/engine.h"
#include <iostream>
#include <atomic>
#include <iomanip>
#include <ShlObj.h>
#include <chrono>
namespace ab=aster::bridge;
using namespace aster;
namespace {
std::atomic<bool> interrupted=false;
BOOL WINAPI interrupt(DWORD signal){if(signal==CTRL_C_EVENT||signal==CTRL_BREAK_EVENT||signal==CTRL_CLOSE_EVENT){interrupted=true;return TRUE;}return FALSE;}
Json vector(ab::Float3 v){return {v.x,v.y,v.z};}
Json motion(const ab::MotionEvent& e){return {{"sequence",e.sequence},{"wall",e.wall},{"scaled",e.scaled!=0},{"flags",e.flags},{"horizontal",e.horizontal},{"vertical",e.vertical},{"previous",vector(e.previous)},{"incoming",vector(e.incoming)},{"requested",vector(e.requested)},{"observedValid",e.observedValid!=0},{"observedImmediately",e.observedValid?vector(e.observed):Json(nullptr)}};}
Json actor(const ab::ActorData& d){return {{"id",d.entityId},{"address",d.address},{"position",vector(d.position)},{"velocity",vector(d.velocity)},{"rotation",{d.rotation.x,d.rotation.y}},{"width",d.width},{"height",d.height},{"minimum",vector(d.minimum)},{"maximum",vector(d.maximum)},{"type",d.type},{"hurtTime",d.hurtTime}};}
Json sample(const ab::Telemetry& t,double elapsed){
    Json modules=Json::object(),actors=Json::array();
    for(unsigned n=0;n<EngineModuleCount;++n){const auto& m=t.modules[n];modules[EngineModuleIds[n]]={{"bound",(t.boundMask&(1u<<n))!=0},{"conditions",m.conditions!=0},{"applied",m.applied!=0},{"requests",m.requests},{"observations",m.observations},{"appliedValue",m.appliedValue},{"observedValue",m.observedValue}};}
    for(unsigned n=0;n<t.actorCount&&n<ab::MaxActors;++n)actors.push_back(actor(t.actors[n]));
    Json cameras=Json::array();for(unsigned n=0;n<t.cameraCount&&n<40;++n){const auto& r=t.cameras[n];cameras.push_back({{"hash",r.hash},{"entity",r.entity},{"address",r.address},{"words",r.words}});}
    const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return {{"wall",wall},{"seconds",elapsed},{"status",static_cast<unsigned>(t.status)},{"error",t.error},{"faults",t.layoutFailures},{"cameras",cameras},{"screen",t.screen},{"foreground",t.foreground!=0},{"permitted",t.permitted!=0},
        {"adapterMeanMicros",t.adapterMeanMicros},{"adapterMaxMicros",t.adapterMaxMicros},{"adapterUpdateHz",t.adapterUpdateHz},{"adapterCollectMicros",t.adapterCollectMicros},{"adapterRayMicros",t.adapterRayMicros},
        {"motionSequence",t.motionSequence},{"lastMotion",t.motionSequence?motion(t.motions[(t.motionSequence-1)%32]):Json(nullptr)},
        {"updateCalls",t.updateCalls},{"gameMode",t.gameMode},{"gameModeVtable",t.gameModeVtable},{"clickRequests",t.clickRequests},{"clickActions",t.clickActions},{"attackArguments",t.attackArguments},{"attackTargetWords",t.attackTargetWords},{"attackExtraWords",t.attackExtraWords},{"cameraRegistry",t.cameraRegistry},
        {"exceptionRva",t.exceptionRva},{"exceptionAddress",t.exceptionAddress},{"localTps",t.measuredLocalTps},{"timerTps",t.timerApplied},{"localTicks",t.localTicks},{"allActorTicks",t.allActorTicks},{"attacks",t.attackCalls},{"attackTargetAddress",t.attackTarget},{"attackArgument",t.attackArgument},
        {"selectedTarget",t.selectedTarget},{"entityTarget",t.entityTarget!=0},{"blockTarget",t.blockTarget!=0},{"blockPosition",{t.blockX,t.blockY,t.blockZ}},{"blockDistance",t.blockDistance},{"rayDistance",t.rayDistance},
        {"aimDelta",{t.aimPitchDelta,t.aimYawDelta}},{"velocityBefore",vector(t.velocityBefore)},{"velocityAfter",vector(t.velocityAfter)},{"local",actor(t.local)},{"actors",actors},{"modules",modules}};
}
void stage(Profile& profile,const Json& s){
    if(!s.contains("modules"))return;
    for(const auto& [id,value]:s.at("modules").items()){
        auto& module=profile.modules.at(id);if(value.contains("enabled"))module.enabled=value.at("enabled").get<bool>();
        if(value.contains("profile"))module.selectedProfile=value.at("profile").get<std::string>();
        if(value.contains("settings"))for(const auto& [key,v]:value.at("settings").items()){
            auto& destination=module.values().at(key);if(std::holds_alternative<bool>(destination))destination=v.get<bool>();else destination=v.get<double>();
        }
    }validate(profile);
}
}
int wmain(int argc,wchar_t** argv){
    try{
        SetConsoleCtrlHandler(interrupt,TRUE);
        InputTimingSession timing;
        if(argc>1&&std::wstring_view(argv[1])==L"--clock-check"){
            HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);if(!timer)throw std::runtime_error("High-resolution timer creation failed");
            std::vector<double> intervals;const auto start=std::chrono::steady_clock::now();auto previous=start;
            for(unsigned i=0;i<500;++i){LARGE_INTEGER due;due.QuadPart=-10000;SetWaitableTimerEx(timer,&due,0,nullptr,nullptr,nullptr,0);WaitForSingleObject(timer,100);auto now=std::chrono::steady_clock::now();intervals.push_back(std::chrono::duration<double,std::milli>(now-previous).count());previous=now;}
            CloseHandle(timer);std::sort(intervals.begin(),intervals.end());std::cout<<Json({{"meanMs",std::chrono::duration<double,std::milli>(previous-start).count()/500.},{"p95Ms",intervals[475]},{"maxMs",intervals.back()}}).dump(2)<<'\n';return 0;
        }
        if(argc>1&&std::wstring_view(argv[1])==L"--peek"){
            PWSTR local=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local);
            auto path=std::filesystem::path(local)/L"Packages"/L"Microsoft.MinecraftUWP_8wekyb3d8bbwe"/L"LocalState"/L"AsterBridge"/L"session.bin";CoTaskMemFree(local);
            HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("No adapter session file");
            HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READWRITE,0,sizeof(ab::Shared),nullptr);if(!mapping){CloseHandle(file);throw std::runtime_error("Cannot map telemetry");}
            auto shared=static_cast<ab::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ab::Shared)));ab::Telemetry t;
            const bool ok=shared&&ab::readConsistent(shared->telemetrySequence,shared->telemetry,t)&&t.magic==ab::Magic&&t.protocol==ab::Protocol;
            ab::Control c;const bool controlOk=shared&&ab::readConsistent(shared->controlSequence,shared->control,c)&&c.magic==ab::Magic&&c.protocol==ab::Protocol;
            if(shared)UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(file);
            if(!ok)throw std::runtime_error("Telemetry snapshot is changing or incompatible; retry");auto output=sample(t,0);Json motions=Json::array();
            for(auto n=t.motionSequence>31?t.motionSequence-31:1;n<=t.motionSequence;++n){const auto& event=t.motions[(n-1)%32];if(event.sequence==n)motions.push_back(motion(event));}output["recentMotionEvents"]=motions;
            if(controlOk)output["control"]={{"armed",c.armed!=0},{"licensed",c.licensed!=0},{"enabledMask",c.enabled},{"expectedWorld",c.expectedWorld}};
            std::cout<<output.dump(2)<<'\n';return 0;
        }
        bool timer=argc>1&&std::wstring_view(argv[1])==L"--timer-test",scenario=argc>2&&std::wstring_view(argv[1])==L"--scenario";
        Json plan;
        if(scenario)plan=Json::parse(readFile(argv[2],1024*1024));
        else if(timer)plan={{"duration",14},{"name","local-timer-20-40-20"},{"stages",Json::array({{{"at",0}},{{"at",4},{"modules",{{"timer",{{"enabled",true},{"settings",{{"multiplier",2.},{"combatMode",false}}}}}}}},{{"at",10},{"modules",{{"timer",{{"enabled",false}}}}}}})}};
        else plan={{"duration",argc>1?std::clamp(_wtoi(argv[1]),1,120):8},{"name","read-only-observation"},{"stages",Json::array()}};
        double duration=std::clamp(plan.at("duration").get<double>(),1.,180.);
        auto game=GameDetector::detectOnce();wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        BridgeSession session;session.connect(game,std::filesystem::path(executable).parent_path()/L"AsterBridge.dll");
        Profile profile=defaultProfile();auto stages=plan.at("stages");std::size_t stageIndex=0;Json active=Json::object();
        ab::Control control;control.probeOnly=!(timer||scenario);control.armed=timer||scenario;
        struct PollTimer {HANDLE value=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);~PollTimer(){if(value)CloseHandle(value);}} pollTimer;
        ab::Telemetry t,last;Json samples=Json::array(),motions=Json::array();std::uint64_t motionSeen=0;auto started=std::chrono::steady_clock::now();double lastSample=-1,lastClick=-100,lastLine=-1;unsigned injectedClicks=0;
        const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const double clockOffset=plan.contains("startAt")?(wall-plan.at("startAt").get<std::int64_t>())/1000.:0.;
        while(!interrupted){
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()+clockOffset;if(elapsed>=duration||!session.alive())break;
            while(stageIndex<stages.size()&&elapsed>=stages.at(stageIndex).at("at").get<double>()){
                active=stages.at(stageIndex++);stage(profile,active);auto next=bridgeConfiguration(profile);next.armed=1;next.probeOnly=0;for(unsigned n=0;n<2;++n){next.physicalPress[n]=control.physicalPress[n];next.outputClicks[n]=control.outputClicks[n];}control=next;
                if(active.contains("mouse"))for(unsigned n=0;n<2;++n){auto held=active.at("mouse").at(n).get<bool>();if(held&&!control.mouse[n])++control.physicalPress[n];control.mouse[n]=held;}
                if(active.contains("mining"))for(unsigned n=0;n<2;++n)control.mining[n]=active.at("mining").at(n).get<bool>();
                std::cout<<"stage "<<stageIndex<<" at "<<elapsed<<'\n';
            }
            if(session.read(t)){last=t;for(auto n=std::max(motionSeen+1,t.motionSequence>31?t.motionSequence-31:1);n<=t.motionSequence;++n){const auto& event=t.motions[(n-1)%32];if(event.sequence==n)motions.push_back(motion(event));}motionSeen=t.motionSequence;}
            const auto cps=active.value("testClicksCps",0.);
            if(cps>0&&last.foreground&&last.status==ab::Status::LayoutVerified&&!strcmp(last.screen,"gameplay_captured")&&elapsed-lastClick>=1./std::clamp(cps,1.,40.)){
                const auto button=active.value("testButton",0);if(button<0||button>1)throw std::runtime_error("testButton must be 0 or 1");++control.outputClicks[button];++injectedClicks;
                const auto period=1./std::clamp(cps,1.,40.);lastClick+=period;if(elapsed-lastClick>=period)lastClick=elapsed;
            }
            session.send(control);
            if(elapsed-lastSample>=.05){samples.push_back(sample(last,elapsed));lastSample=elapsed;}
            if(elapsed-lastLine>=1){std::cout<<"t="<<std::fixed<<std::setprecision(2)<<elapsed<<" state="<<static_cast<unsigned>(last.status)<<" screen="<<last.screen<<" TPS="<<last.measuredLocalTps<<" attacks="<<last.attackCalls<<" faults="<<last.layoutFailures<<'\n';lastLine=elapsed;}
            if(pollTimer.value){LARGE_INTEGER due;due.QuadPart=-10000;if(SetWaitableTimerEx(pollTimer.value,&due,0,nullptr,nullptr,nullptr,0))WaitForSingleObject(pollTimer.value,25);else Sleep(1);}else Sleep(1);
        }
        session.close();
        Json report={{"name",plan.value("name","scenario")},{"appVersion",kVersion},{"build",game.build},{"sha256",kSupportedGameSha256},{"protocol",2225},{"bridgeProtocol",ab::Protocol},{"plan",plan},{"testQueuedClicks",injectedClicks},{"interrupted",interrupted.load()},{"samples",samples},{"motionEvents",motions}};
        auto output=argc>3&&scenario?std::filesystem::path(argv[3]):std::filesystem::current_path()/L"test-results"/(timer?L"timer-world-test.json":L"engine-probe.json");
        std::filesystem::create_directories(output.parent_path());atomicWrite(output,report.dump(2));std::cout<<"Adapter stopped; report saved.\n";return last.layoutFailures?2:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
