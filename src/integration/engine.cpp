#include "integration/engine.h"
#include "platform/windows.h"
#include "auth/license.h"
#include <TlHelp32.h>
#include <ShlObj.h>
#include <algorithm>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace aster {
namespace ab=bridge;
namespace {
struct WinHandle {
    HANDLE value=nullptr;
    ~WinHandle(){reset();}
    void reset(HANDLE h=nullptr){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=h;}
    explicit operator bool()const{return value&&value!=INVALID_HANDLE_VALUE;}
};
std::uintptr_t remoteLoader(DWORD pid){
    auto fn=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");MEMORY_BASIC_INFORMATION memory{};
    if(!fn||!VirtualQuery(reinterpret_cast<void*>(fn),&memory,sizeof(memory)))throw std::runtime_error("Cannot resolve the Windows loader");
    wchar_t path[32768]{};GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),path,32768);
    auto name=std::filesystem::path(path).filename().wstring();
    WinHandle list;list.reset(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(!list)throw std::runtime_error("Cannot inspect the game module list");
    MODULEENTRY32W module{};module.dwSize=sizeof(module);std::uintptr_t loader=0;
    if(Module32FirstW(list.value,&module))do{
        if(_wcsicmp(module.szModule,L"AsterBridge.dll")==0)throw std::runtime_error("An Aster bridge is already loaded; wait for its owner to stop");
        if(_wcsicmp(module.szModule,name.c_str())==0)loader=reinterpret_cast<std::uintptr_t>(module.modBaseAddr)+(reinterpret_cast<std::uintptr_t>(fn)-reinterpret_cast<std::uintptr_t>(memory.AllocationBase));
    }while(Module32NextW(list.value,&module));
    if(!loader)throw std::runtime_error("Game loader module is unavailable");return loader;
}
std::filesystem::path runningExecutable(){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);return path;}
}
struct BridgeSession::Impl {
    WinHandle process,owner,file,mapping;
    ab::Shared* memory=nullptr;
    std::uint64_t session=0;
    ab::Control control;
    ~Impl(){if(memory)UnmapViewOfFile(memory);}
};
BridgeSession::BridgeSession():impl_(std::make_unique<Impl>()){}
BridgeSession::~BridgeSession(){close();}
void BridgeSession::connect(const GameSnapshot& game,const std::filesystem::path& source){
    close();
    if(!game.identityVerified||!game.x64||game.build!=kInputResearchBuild||game.imagePath.empty())throw std::runtime_error("Exact Store x64 build 1.26.6005.0 is required");
    auto next=std::make_unique<Impl>();
    next->owner.reset(CreateMutexW(nullptr,FALSE,(L"Local\\AsterBridge.Owner."+std::to_wstring(game.pid)).c_str()));
    if(!next->owner||GetLastError()==ERROR_ALREADY_EXISTS)throw std::runtime_error("Another Aster process owns this game's adapter");
    if(sha256(readFile(wide(game.imagePath),400*1024*1024))!=kSupportedGameSha256)throw std::runtime_error("Executable SHA-256 differs from the tested build; adapter refused");
    const auto loader=remoteLoader(game.pid);
    PWSTR local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local)))throw std::runtime_error("Local app-data folder is unavailable");
    auto folder=std::filesystem::path(local)/L"Packages"/L"Microsoft.MinecraftUWP_8wekyb3d8bbwe"/L"LocalState"/L"AsterBridge";CoTaskMemFree(local);
    std::filesystem::create_directories(folder);auto dll=folder/L"AsterBridge.dll";
    std::filesystem::copy_file(source,dll,std::filesystem::copy_options::overwrite_existing);
    next->file.reset(CreateFileW((folder/L"session.bin").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!next->file)throw std::runtime_error("Cannot open Aster's shared session file");
    next->mapping.reset(CreateFileMappingW(next->file.value,nullptr,PAGE_READWRITE,0,sizeof(ab::Shared),nullptr));
    if(!next->mapping)throw std::runtime_error("Cannot create the adapter mapping");
    next->memory=static_cast<ab::Shared*>(MapViewOfFile(next->mapping.value,FILE_MAP_ALL_ACCESS,0,0,sizeof(ab::Shared)));
    if(!next->memory)throw std::runtime_error("Cannot map the adapter session");
    memset(next->memory,0,sizeof(ab::Shared));next->session=GetTickCount64()^game.creationTime;
    next->control.session=next->session;next->control.heartbeat=GetTickCount64();
    ab::writeConsistent(next->memory->controlSequence,next->memory->control,next->control);
    next->process.reset(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|PROCESS_QUERY_INFORMATION|SYNCHRONIZE,FALSE,game.pid));
    if(!next->process)throw std::runtime_error("Windows denied the adapter connection; no bypass is attempted");
    const auto path=dll.wstring();const auto size=(path.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(next->process.value,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);SIZE_T written=0;
    if(!remote)throw std::runtime_error("Cannot allocate the loader argument");
    if(!WriteProcessMemory(next->process.value,remote,path.c_str(),size,&written)||written!=size){VirtualFreeEx(next->process.value,remote,0,MEM_RELEASE);throw std::runtime_error("Cannot write the loader argument");}
    WinHandle thread;thread.reset(CreateRemoteThread(next->process.value,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(loader),remote,0,nullptr));
    if(!thread){VirtualFreeEx(next->process.value,remote,0,MEM_RELEASE);throw std::runtime_error("Windows refused the adapter loader");}
    if(WaitForSingleObject(thread.value,3000)!=WAIT_OBJECT_0)throw std::runtime_error("Adapter loader timed out; its pending argument was retained");
    DWORD code=0;GetExitCodeThread(thread.value,&code);VirtualFreeEx(next->process.value,remote,0,MEM_RELEASE);
    if(!code)throw std::runtime_error("Windows could not load AsterBridge.dll");
    impl_=std::move(next);
}
void BridgeSession::send(ab::Control control){if(!impl_->memory)return;control.session=impl_->session;control.heartbeat=GetTickCount64();impl_->control=control;ab::writeConsistent(impl_->memory->controlSequence,impl_->memory->control,control);}
bool BridgeSession::read(ab::Telemetry& result)const{return impl_->memory&&ab::readConsistent(impl_->memory->telemetrySequence,impl_->memory->telemetry,result)&&result.magic==ab::Magic&&result.protocol==ab::Protocol;}
bool BridgeSession::alive()const{return impl_->process&&WaitForSingleObject(impl_->process.value,0)==WAIT_TIMEOUT;}
std::uint64_t BridgeSession::id()const{return impl_->session;}
void BridgeSession::close(){
    if(impl_->memory){auto c=impl_->control;c.shutdown=1;c.armed=0;c.timerEnabled=0;send(c);
        const auto until=GetTickCount64()+1500;ab::Telemetry t;
        while(alive()&&GetTickCount64()<until){if(read(t)&&t.status==ab::Status::Stopped)break;Sleep(10);}}
    impl_=std::make_unique<Impl>();
}
ab::Control bridgeConfiguration(const Profile& p){
    ab::Control c;c.probeOnly=0;
    for(unsigned n=0;n<EngineModuleCount;++n)if(p.modules.at(std::string(EngineModuleIds[n])).enabled)c.enabled|=1u<<n;
    const auto& reach=p.modules.at("reach"),&aim=p.modules.at("aim"),&box=p.modules.at("hitbox"),&velocity=p.modules.at("velocity"),&timer=p.modules.at("timer"),&spin=p.modules.at("auto360"),&zoom=p.modules.at("zoom");
    c.reach=static_cast<float>(reach.number("distance"));c.boxWidth=static_cast<float>(box.number("width"));c.timerMultiplier=static_cast<float>(timer.number("multiplier"));
    c.aimSmooth=static_cast<float>(aim.number("smoothness"));c.aimOuter=static_cast<float>(aim.number("fovHorizontal"));c.aimInner=static_cast<float>(aim.number("fovVertical"));c.aimDistance=static_cast<float>(aim.number("distance"));c.aimHeight=static_cast<float>(aim.number("height"));
    c.velocityHorizontal=static_cast<float>(velocity.number("horizontal")/100.);c.velocityVertical=static_cast<float>(velocity.number("vertical")/100.);
    c.spinSpeed=static_cast<float>(spin.number("speed"));c.spinLeft=static_cast<float>(spin.number("leftPercent"));c.spinRight=static_cast<float>(spin.number("rightPercent"));c.spinStep=static_cast<float>(spin.number("step"));c.spinInterval=static_cast<float>(spin.number("interval"));c.zoomSpeed=static_cast<float>(zoom.number("speedPercent"));
    auto flag=[&](bool value,ab::Flag bit){if(value)c.flags|=bit;};
    flag(reach.flag("combatMode"),ab::ReachCombat);flag(reach.flag("onlyMoving"),ab::ReachMoving);
    flag(aim.flag("vertical"),ab::AimVertical);flag(aim.flag("horizontal"),ab::AimHorizontal);flag(aim.flag("antibot"),ab::AimPlayers);flag(aim.flag("onlyMoving"),ab::AimMoving);flag(aim.flag("avoidWalls"),ab::AimWalls);flag(aim.flag("avoidOnTarget"),ab::AimOnTarget);flag(aim.flag("cacheTargets"),ab::AimCache);flag(aim.flag("combatMode"),ab::AimCombat);flag(aim.selectedProfile=="blatant",ab::AimBlatant);
    flag(box.flag("deterministic"),ab::BoxDeterministic);flag(velocity.flag("combatMode"),ab::VelocityCombat);flag(velocity.flag("combineModifiers"),ab::VelocityCombine);flag(velocity.flag("noBackwards"),ab::VelocityNoBackwards);flag(timer.flag("combatMode"),ab::TimerCombat);flag(p.modules.at("clicker").flag("invert"),ab::InvertClicks);flag(zoom.flag("lowCpuMode"),ab::ZoomLowCpu);
    return c;
}
struct EngineService::Impl {
    GameDetector& detector;Diagnostics& log;mutable std::mutex mutex;
    ab::Control control;PhysicalInput physical;std::array<int,6> movement{};EngineRuntime runtime;
    BridgeSession session;std::jthread worker;
    Impl(GameDetector& d,Diagnostics& l):detector(d),log(l),worker([this](std::stop_token stop){run(stop);}){}
    ~Impl(){worker.request_stop();if(worker.joinable())worker.join();}
    void run(std::stop_token stop){
        DWORD attempted=0;std::uint64_t attemptCreation=0;ab::Status previous=ab::Status::Waiting;
        while(!stop.stop_requested()){
            auto game=detector.snapshot();
            if(!game.present||!session.alive()){
                if(session.id())session.close();
                if(game.present&&game.identityVerified&&(attempted!=game.pid||attemptCreation!=game.creationTime)){
                    attempted=game.pid;attemptCreation=game.creationTime;
                    try{session.connect(game,runningExecutable().parent_path()/L"AsterBridge.dll");log.event("adapter","ADAPTER_ATTACHED","Exact executable hash verified; native bridge connected.","DETECTED -> BOUND",game.build);}
                    catch(const std::exception& e){std::lock_guard lock(mutex);runtime={};runtime.detail=e.what();log.event("adapter","ADAPTER_REFUSED",e.what(),"DETECTED -> UNSUPPORTED",game.build);}
                }else if(!game.present){attempted=0;attemptCreation=0;std::lock_guard lock(mutex);runtime={};runtime.detail="Waiting for Minecraft";}
            }
            if(session.alive()){
                ab::Control next;{std::lock_guard lock(mutex);next=control;}
                session.send(next);ab::Telemetry t;
                if(session.read(t)){
                    EngineRuntime r;r.session=session.id();r.sha256=kSupportedGameSha256;r.buildProtocol=2225;
                    r.heartbeatAge=GetTickCount64()-t.heartbeat;r.bound=t.status==ab::Status::Hooked||t.status==ab::Status::WorldDetected||t.status==ab::Status::LayoutVerified;
                    r.worldReady=t.status==ab::Status::LayoutVerified&&r.heartbeatAge<500;r.permitted=t.permitted!=0&&r.worldReady;
                    r.screen=t.screen;r.world=t.worldGeneration;r.attackCalls=t.attackCalls;r.localTps=t.measuredLocalTps;
                    for(unsigned n=0;n<2;++n){r.clickRequests[n]=t.clickRequests[n];r.clickActions[n]=t.clickActions[n];}
                    r.blockTarget=t.blockTarget!=0;r.entityTarget=t.entityTarget!=0;
                    r.blockToken=(static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.blockX))*73856093u)^(static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.blockY))*19349663u)^(static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.blockZ))*83492791u)^t.worldGeneration;
                    r.detail=!r.bound?"Native adapter failed its signature/layout checks":!r.worldReady?"Adapter bound; waiting for an active world":r.permitted?"Native adapter running in the focused world":r.screen!="gameplay_captured"?"Gameplay paused: "+r.screen:"Adapter bound; press F8 in Minecraft to arm";
                    for(unsigned n=0;n<EngineModuleCount;++n){auto& m=r.modules[n];const auto& e=t.modules[n];m.bound=r.bound&&(t.boundMask&(1u<<n));m.requested=(next.enabled&(1u<<n))!=0;m.conditions=e.conditions!=0&&r.permitted;m.applied=e.applied!=0&&r.permitted;m.requests=e.requests;m.observations=e.observations;m.appliedValue=e.appliedValue;m.observedValue=e.observedValue;m.reason=!m.bound?"Native handler unavailable":!r.permitted?r.detail:!m.conditions?"Waiting for module conditions":m.applied?"Native action applied; observations are counted separately":"Handler ready for an eligible action";}
                    if(t.status!=previous){log.event("adapter","ADAPTER_STATE",r.detail,"",game.build);previous=t.status;}
                    {std::lock_guard lock(mutex);runtime=std::move(r);}
                }
            }
            Sleep(10);
        }
        session.close();
    }
};
EngineService::EngineService(GameDetector& d,Diagnostics& l):impl_(std::make_unique<Impl>(d,l)){}
EngineService::~EngineService()=default;
void EngineService::configure(const Profile& p){std::lock_guard lock(impl_->mutex);auto next=bridgeConfiguration(p);next.armed=impl_->control.armed;next.licensed=impl_->control.licensed;next.expectedWorld=impl_->control.expectedWorld;next.mouse[0]=impl_->control.mouse[0];next.mouse[1]=impl_->control.mouse[1];next.movement=impl_->control.movement;for(unsigned n=0;n<2;++n){next.physicalPress[n]=impl_->control.physicalPress[n];next.outputClicks[n]=impl_->control.outputClicks[n];next.mining[n]=impl_->control.mining[n];}impl_->control=next;impl_->movement=p.movement;}
void EngineService::input(const PhysicalInput& p,bool armed,bool licensed,const std::array<std::uint64_t,2>& clicks,const std::array<bool,2>& mining,std::uint64_t world){
    std::lock_guard lock(impl_->mutex);auto& c=impl_->control;c.armed=armed;c.licensed=licensed;c.expectedWorld=world;
    c.movement=0;for(unsigned n=0;n<6;++n)if(p.keys[impl_->movement[n]])c.movement|=1u<<n;
    for(unsigned n=0;n<2;++n){c.mouse[n]=p.mouse[n];c.physicalPress[n]=p.pressSequence[n];c.outputClicks[n]=clicks[n];c.mining[n]=mining[n];}
}
EngineRuntime EngineService::status()const{std::lock_guard lock(impl_->mutex);return impl_->runtime;}
}
