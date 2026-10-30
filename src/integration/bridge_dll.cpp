#include <Windows.h>
#include <MinHook.h>
#include "integration/components.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace ab=aster::bridge;
namespace {
HMODULE selfModule=nullptr;
ab::Shared* shared=nullptr;
ab::Telemetry telemetry;
std::atomic<unsigned> activeCalls=0;
std::atomic<void*> client=nullptr;
std::atomic<void*> localActor=nullptr;
std::atomic<std::uint64_t> localTicks=0;
std::atomic<std::uint64_t> allTicks=0;
std::atomic_flag collecting=ATOMIC_FLAG_INIT;
std::atomic<bool> stopping=false;
using UpdateFn=bool(__fastcall*)(void*,bool,bool);
using PlayerFn=void*(__fastcall*)(void*);
using TickFn=void(__fastcall*)(void*);
UpdateFn originalUpdate=nullptr;
PlayerFn getPlayer=nullptr;
TickFn originalTick=nullptr;
float* ownedTimer=nullptr;
float initialTimer=20.f,lastTimer=20.f;
std::uintptr_t moduleBase=0,moduleEnd=0;

bool readable(const void* pointer,std::size_t bytes){
    auto address=reinterpret_cast<std::uintptr_t>(pointer);if(address<0x10000||address+bytes<address)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(pointer,&info,sizeof(info))||info.State!=MEM_COMMIT||info.Protect&(PAGE_GUARD|PAGE_NOACCESS))return false;
    return address+bytes<=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
}
bool finite(ab::Float3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<30000001&&std::abs(p.z)<30000001&&std::abs(p.y)<100000;}
bool writable(const void* pointer,std::size_t bytes){
    if(!readable(pointer,bytes))return false;MEMORY_BASIC_INFORMATION info{};VirtualQuery(pointer,&info,sizeof(info));
    return (info.Protect&0xff)==PAGE_READWRITE||(info.Protect&0xff)==PAGE_EXECUTE_READWRITE;
}
bool gameForeground(){
    auto window=GetForegroundWindow();DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId())return true;
    bool matches=false;EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{DWORD owner=0;GetWindowThreadProcessId(child,&owner);
        if(owner==GetCurrentProcessId()){*reinterpret_cast<bool*>(data)=true;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&matches));return matches;
}
void restoreTimer(){
    if(ownedTimer&&writable(ownedTimer,sizeof(float))&&std::abs(*ownedTimer-lastTimer)<.001f)*ownedTimer=initialTimer;
    ownedTimer=nullptr;telemetry.timerApplied=20.f;
}
void timerControl(void* ci,const ab::Control& control){
    const bool enabled=!stopping.load()&&!control.probeOnly&&control.timerEnabled&&!control.shutdown&&control.heartbeat+3000>=GetTickCount64()&&telemetry.status==ab::Status::LayoutVerified&&localActor.load()&&gameForeground();
    if(!enabled){restoreTimer();return;}
    auto minecraft=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(ci)+0x1D0);
    if(!readable(minecraft,0xF0)){restoreTimer();return;}
    auto timer=*reinterpret_cast<float**>(minecraft+0xE8);
    if(!writable(timer,32)||!std::isfinite(timer[0])||timer[2]<-.1f||timer[2]>1.1f){restoreTimer();return;}
    if(ownedTimer&&ownedTimer!=timer)restoreTimer();
    if(!ownedTimer){if(std::abs(timer[0]-20.f)>.001f)return;ownedTimer=timer;initialTimer=timer[0];}
    if(std::abs(timer[0]-lastTimer)>.001f&&std::abs(timer[0]-initialTimer)>.001f){ownedTimer=nullptr;return;}
    lastTimer=20.f*std::clamp(control.timerMultiplier,1.f,5.f);timer[0]=lastTimer;telemetry.timerApplied=timer[0];
}
std::uintptr_t scanUnique(const char* text){
    std::vector<int> pattern;
    while(*text){if(*text==' '){++text;continue;}if(*text=='?'){pattern.push_back(-1);++text;if(*text=='?')++text;}
        else{char token[3]{text[0],text[1],0};pattern.push_back(static_cast<int>(strtoul(token,nullptr,16)));text+=2;}}
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(moduleBase+dos->e_lfanew);
    auto section=IMAGE_FIRST_SECTION(nt);std::uintptr_t found=0;
    for(unsigned n=0;n<nt->FileHeader.NumberOfSections;++n){
        if(!(section[n].Characteristics&IMAGE_SCN_MEM_EXECUTE))continue;
        auto begin=moduleBase+section[n].VirtualAddress;auto length=section[n].Misc.VirtualSize;
        for(std::size_t i=0;i+pattern.size()<=length;++i){auto data=reinterpret_cast<unsigned char*>(begin+i);
            if(data[0]!=pattern[0])continue;bool matches=true;
            for(std::size_t p=1;p<pattern.size();++p)if(pattern[p]>=0&&data[p]!=pattern[p]){matches=false;break;}
            if(matches){if(found)return 0;found=begin+i;}
        }
    }return found;
}
bool actorData(const void* actor,ab::ActorData& result,const entt::basic_registry<EntityId>* expected=nullptr){
    telemetry.error=1001;if(!readable(actor,0x30))return false;
    auto bytes=reinterpret_cast<const unsigned char*>(actor);
    auto table=*reinterpret_cast<const std::uintptr_t* const*>(actor);
    telemetry.error=1002;if(!readable(table,sizeof(void*))||table[0]<moduleBase||table[0]>=moduleEnd)return false;
    auto registry=*reinterpret_cast<const entt::basic_registry<EntityId>* const*>(bytes+0x10);
    auto entity=*reinterpret_cast<const EntityId*>(bytes+0x18);
    telemetry.error=1003;if(!readable(registry,64)||(expected&&registry!=expected))return false;
    telemetry.error=1004;if(!registry->valid(entity))return false;
    // Only const registry operations: never creates a missing game component pool.
    auto state=registry->try_get<StateVectorComponent>(entity);
    auto rotation=registry->try_get<ActorRotationComponent>(entity);
    auto shape=registry->try_get<AABBShapeComponent>(entity);
    telemetry.error=1005;if(!readable(state,sizeof(*state))||!readable(rotation,sizeof(*rotation))||!readable(shape,sizeof(*shape)))return false;
    telemetry.error=1006;if(!finite(state->position)||!finite(state->previous)||!finite(state->velocity)||!finite(shape->minimum)||!finite(shape->maximum))return false;
    telemetry.error=1007;if(!(shape->width>.01f&&shape->width<30.f&&shape->height>.01f&&shape->height<100.f))return false;
    if(!std::isfinite(rotation->rotation.x)||!std::isfinite(rotation->rotation.y)||std::abs(rotation->rotation.x)>181||std::abs(rotation->rotation.y)>10000000)return false;
    result.address=reinterpret_cast<std::uint64_t>(actor);result.entityId=static_cast<std::uint32_t>(entity);
    result.position=state->position;result.velocity=state->velocity;result.rotation=rotation->rotation;result.width=shape->width;result.height=shape->height;
    if(auto type=registry->try_get<ActorTypeComponent>(entity);readable(type,sizeof(*type)))result.type=type->type;
    telemetry.error=0;return true;
}
void timerCandidates(void* ci){
    telemetry.timerCount=0;
    // This is observation only: candidate pointers must later be verified against tick behavior.
    for(unsigned offset=0x140;offset<=0x240;offset+=8){
        auto ptr=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(ci)+offset);
        if(!readable(ptr,0x120))continue;
        for(unsigned inner=0xC0;inner<=0x100;inner+=8){
            auto timer=*reinterpret_cast<float**>(ptr+inner);if(!readable(timer,32))continue;
            if((timer[0]!=20.f&&timer!=ownedTimer)||!std::isfinite(timer[1])||!std::isfinite(timer[2])||timer[2]<-.1f||timer[2]>1.1f)continue;
            if(telemetry.timerCount>=12)return;
            auto& candidate=telemetry.timers[telemetry.timerCount++];candidate.clientOffset=offset;candidate.minecraftOffset=inner;
            candidate.address=reinterpret_cast<std::uint64_t>(timer);memcpy(candidate.data,timer,sizeof(candidate.data));
        }
    }
}
bool collect(void* ci){
    if(!readable(ci,0x300))return false;
    auto player=getPlayer(ci);telemetry.client=reinterpret_cast<std::uint64_t>(ci);telemetry.localPlayer=reinterpret_cast<std::uint64_t>(player);
    localActor.store(player);
    auto ciTable=*reinterpret_cast<std::uintptr_t**>(ci);
    if(readable(ciTable,sizeof(telemetry.clientVtable)))for(unsigned i=0;i<64;++i)telemetry.clientVtable[i]=ciTable[i]>=moduleBase&&ciTable[i]<moduleEnd?ciTable[i]-moduleBase:0;
    timerCandidates(ci);
    if(!player){telemetry.actorCount=0;telemetry.status=ab::Status::Hooked;return true;}
    telemetry.status=ab::Status::WorldDetected;
    if(!actorData(player,telemetry.local))return false;
    const auto registry=*reinterpret_cast<const entt::basic_registry<EntityId>* const*>(static_cast<unsigned char*>(player)+0x10);
    telemetry.actorCount=0;
    if(auto owners=registry->storage<ActorOwnerComponent>();owners){
        for(auto [entity,owner]:owners->each()){
            (void)entity;if(telemetry.actorCount>=ab::MaxActors)break;
            ab::ActorData data;if(actorData(owner.actor,data,registry))telemetry.actors[telemetry.actorCount++]=data;
        }
    }
    telemetry.gameMode=0;
    for(unsigned offset=0xA00;offset<=0xB00;offset+=8){
        auto gm=*reinterpret_cast<void**>(static_cast<unsigned char*>(player)+offset);if(!readable(gm,32))continue;
        auto table=*reinterpret_cast<std::uintptr_t**>(gm);if(!readable(table,24*sizeof(void*)))continue;
        if(table[0]<moduleBase||table[0]>=moduleEnd)continue;
        // The GameMode owns a pointer back to the local player immediately after its vptr.
        if(*reinterpret_cast<void**>(static_cast<unsigned char*>(gm)+8)!=player)continue;
        telemetry.gameMode=reinterpret_cast<std::uint64_t>(gm);
        for(unsigned i=0;i<24;++i)telemetry.gameModeVtable[i]=table[i]>=moduleBase&&table[i]<moduleEnd?table[i]-moduleBase:0;
        break;
    }
    telemetry.status=ab::Status::LayoutVerified;return true;
}
bool safeCollect(void* ci){__try{return collect(ci);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
void safeTimerControl(void* ci,const ab::Control* control){__try{timerControl(ci,*control);}__except(EXCEPTION_EXECUTE_HANDLER){ownedTimer=nullptr;telemetry.error=2001;}}
void __fastcall tickHook(void* actor){++activeCalls;originalTick(actor);++allTicks;if(actor==localActor.load())++localTicks;--activeCalls;}
bool __fastcall updateHook(void* ci,bool initialized,bool allowUpdate){
    ++activeCalls;bool result=originalUpdate(ci,initialized,allowUpdate);client.store(ci);
    if(!collecting.test_and_set(std::memory_order_acquire)){
        ++telemetry.updateCalls;auto now=GetTickCount64();
        ab::Control control;if(shared&&ab::readConsistent(shared->controlSequence,shared->control,control))safeTimerControl(ci,&control);
        if(shared&&now-telemetry.heartbeat>=100){
            telemetry.heartbeat=now;
            if(!safeCollect(ci)){++telemetry.layoutFailures;telemetry.status=ab::Status::LayoutFailure;}
            telemetry.localTicks=localTicks.load();
            telemetry.allActorTicks=allTicks.load();telemetry.foreground=gameForeground()?1u:0u;
            static auto ticksAtStart=localTicks.load();static auto periodStart=now;
            if(now-periodStart>=1000){telemetry.measuredLocalTps=static_cast<float>((localTicks.load()-ticksAtStart)*1000.)/static_cast<float>(now-periodStart);ticksAtStart=localTicks.load();periodStart=now;}
            ab::writeConsistent(shared->telemetrySequence,shared->telemetry,telemetry);
        }
        collecting.clear(std::memory_order_release);
    }
    --activeCalls;return result;
}
DWORD WINAPI worker(void*){
    wchar_t filename[32768]{};GetModuleFileNameW(selfModule,filename,32768);std::wstring path(filename);path.resize(path.find_last_of(L"\\/")+1);path+=L"session.bin";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(file==INVALID_HANDLE_VALUE)return 0;
    HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READWRITE,0,sizeof(ab::Shared),nullptr);
    if(!mapping){CloseHandle(file);return 0;}
    shared=static_cast<ab::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ab::Shared)));
    ab::Control control;
    if(!shared||!ab::readConsistent(shared->controlSequence,shared->control,control)||control.magic!=ab::Magic||control.protocol!=ab::Protocol){
        if(shared)UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(file);return 0;}
    moduleBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(moduleBase+dos->e_lfanew);moduleEnd=moduleBase+nt->OptionalHeader.SizeOfImage;
    auto update=scanUnique("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? FE FF FF FF 45 89 C6 89 D3 48 89 CE 48 8B 01 48 8B 80 ? ? ? ? 48 8D 95 ? ? ? ? FF 15");
    auto player=scanUnique("48 83 EC ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 48 8D 91 68 02 00 00 48 8D 4C 24 ? E8 ? ? ? ? 80 7C 24 ? 01 75 ? 48 8D 4C 24 ? B2 01");
    auto tick=scanUnique("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 85 ? ? ? ? 66 0F 7F BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 CE 80 B9 ? ? ? ? 01 75 ? 83 BE ? ? ? ? 00 7F ? 48 8B 8E ? ? ? ? 48 8B 01");
    getPlayer=reinterpret_cast<PlayerFn>(player);
    bool installed=update&&player&&MH_Initialize()==MH_OK&&MH_CreateHook(reinterpret_cast<void*>(update),updateHook,reinterpret_cast<void**>(&originalUpdate))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(update))==MH_OK;
    bool tickInstalled=installed&&tick&&MH_CreateHook(reinterpret_cast<void*>(tick),tickHook,reinterpret_cast<void**>(&originalTick))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(tick))==MH_OK;
    telemetry.status=installed?ab::Status::Hooked:ab::Status::SignatureFailure;telemetry.heartbeat=GetTickCount64();
    ab::writeConsistent(shared->telemetrySequence,shared->telemetry,telemetry);
    if(installed){
        for(;;){Sleep(50);ab::Control next;
            if(ab::readConsistent(shared->controlSequence,shared->control,next))control=next;
            if(control.shutdown||GetTickCount64()>control.heartbeat+3000)break;
        }
        // Give the game thread a chance to restore the owned timer before removing observers.
        stopping.store(true);Sleep(100);
        MH_DisableHook(reinterpret_cast<void*>(update));if(tickInstalled)MH_DisableHook(reinterpret_cast<void*>(tick));
        auto until=GetTickCount64()+2000;while(activeCalls&&GetTickCount64()<until)Sleep(10);
        if(activeCalls)return 0; // Keep the disabled DLL resident rather than unloading under a live callback.
        Sleep(50); // Disabled detours cannot re-enter; allow completed callback epilogues to leave this image.
        restoreTimer();MH_RemoveHook(reinterpret_cast<void*>(update));if(tickInstalled)MH_RemoveHook(reinterpret_cast<void*>(tick));MH_Uninitialize();
    }
    telemetry.status=ab::Status::Stopped;ab::writeConsistent(shared->telemetrySequence,shared->telemetry,telemetry);
    UnmapViewOfFile(shared);shared=nullptr;CloseHandle(mapping);CloseHandle(file);FreeLibraryAndExitThread(selfModule,0);
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){selfModule=module;DisableThreadLibraryCalls(module);if(auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr))CloseHandle(thread);}
    return TRUE;
}
