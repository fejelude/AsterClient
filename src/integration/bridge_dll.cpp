#include <Windows.h>
#include <MinHook.h>
#include "integration/components.h"
#include "core/engine_math.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include <numbers>
#include <random>
#include <mutex>

namespace ab=aster::bridge;
extern "C" {
void* AsterCameraTrampoline=nullptr;
void* AsterAttackTrampoline=nullptr;
void AsterCameraObserverEntry();
void AsterAttackObserverEntry();
}
namespace {
HMODULE selfModule=nullptr;
ab::Shared* shared=nullptr;
ab::Telemetry telemetry;
std::atomic<unsigned> activeCalls=0;
std::atomic<void*> client=nullptr;
std::atomic<void*> localActor=nullptr;
std::atomic<void*> localGameMode=nullptr;
std::atomic<bool> worldVerified=false;
std::atomic<std::uint64_t> worldEpoch=0;
std::uint64_t activeSession=0;
std::atomic<std::uint64_t> localTicks=0;
std::atomic<std::uint64_t> allTicks=0;
std::atomic_flag collecting=ATOMIC_FLAG_INIT;
std::atomic<bool> stopping=false;
std::atomic<bool> faulted=false;
struct MemoryRange {std::uintptr_t begin=0,end=0;};
thread_local MemoryRange readRanges[32]{};
thread_local unsigned readRangeCount=0,callbackDepth=0;
struct CallbackLifetime {CallbackLifetime(){if(callbackDepth++==0)readRangeCount=0;++activeCalls;}~CallbackLifetime(){--activeCalls;--callbackDepth;}};
using UpdateFn=std::uint64_t(__fastcall*)(void*,std::uint64_t,std::uint64_t,std::uint64_t);
using PlayerFn=void*(__fastcall*)(void*);
using TickFn=void(__fastcall*)(void*);
using ReceiveFn=void(__fastcall*)(void*,void*,void*,void*);
ReceiveFn originalReceive=nullptr;
struct BlockPos;
using BuildFn=bool(__fastcall*)(void*,const BlockPos&,unsigned char,unsigned char,bool);
BuildFn originalBuild=nullptr;
std::atomic<std::uint64_t> buildAccepted=0;
using MouseCaptureFn=void(__fastcall*)(void*);
MouseCaptureFn originalGrab=nullptr,originalRelease=nullptr;
std::atomic<bool> captured=false;
UpdateFn originalUpdate=nullptr;
PlayerFn getPlayer=nullptr;
TickFn originalTick=nullptr;
using AttackFn=bool(__fastcall*)(void*,void*,const ab::Float3&);
AttackFn originalAttack=nullptr;
std::atomic<std::uint64_t> attackCalls=0,attackTarget=0,attackArgument=0;
std::atomic<void*> regionPointer=nullptr;
std::atomic<const entt::basic_registry<EntityId>*> cameraRegistry=nullptr;
std::atomic<std::uint64_t> velocityChanges=0,velocityObservations=0,velocityLastApplied=0;
std::mutex velocityMutex,attackMutex;
struct AttackObservation {std::uint64_t arguments[4]{};std::uint32_t target[16]{},extra[16]{};} attackObservation;
ab::Float3 velocityBefore{},velocityAfter{};
ab::MotionEvent motionEvents[32]{};std::uint64_t motionSequence=0;
ab::Control currentControl;
std::uint64_t observedAttack=0,lastPhysical=0,lastOutput[2]{},lastDispatchTime=0,lastDispatchTarget=0,lastWorldActor=0,lastWorldRegistry=0;
std::uint32_t lastWorldEntity=0;
double lastFrame=0,spinRemaining=0,spinPending=0,nextSpin=0;
int spinDirection=1;
std::uint64_t spinAttack=0,cachedAim=0,cachedRay=0;
bool aimPending=false,spinMeasuring=false,spinFinished=false;aster::Orientation aimExpected{};double spinPreviousYaw=0,spinTravel=0;
std::mt19937 randomDirection{0xA57E};
float* ownedTimer=nullptr;
float initialTimer=20.f,lastTimer=20.f;
float* ownedFov=nullptr;float initialFov=0,lastFov=0;double zoomWeight=0;
std::uintptr_t moduleBase=0,moduleEnd=0;
struct BlockNameRecord {void* block=nullptr;char name[64]{};};
BlockNameRecord blockNames[128]{};unsigned blockNameNext=0;
double rayMicros=0;

bool readable(const void* pointer,std::size_t bytes){
    auto address=reinterpret_cast<std::uintptr_t>(pointer);if(address<0x10000||address+bytes<address)return false;
    if(callbackDepth)for(unsigned n=0;n<readRangeCount;++n)if(address>=readRanges[n].begin&&address+bytes<=readRanges[n].end)return true;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(pointer,&info,sizeof(info))||info.State!=MEM_COMMIT||info.Protect&(PAGE_GUARD|PAGE_NOACCESS))return false;
    const auto end=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
    if(address+bytes>end)return false;
    // Reuse committed readable ranges only during this callback. Exceptions remain
    // guarded, writable checks stay fresh, and the next outer callback starts empty.
    if(callbackDepth&&readRangeCount<32)readRanges[readRangeCount++]={reinterpret_cast<std::uintptr_t>(info.BaseAddress),end};
    return true;
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
aster::Vec3 vec(ab::Float3 p){return {p.x,p.y,p.z};}
ab::Float3 packed(aster::Vec3 p){return {static_cast<float>(p.x),static_cast<float>(p.y),static_cast<float>(p.z)};}
aster::Box box(const ab::ActorData& a){return {vec(a.minimum),vec(a.maximum)};}
aster::Orientation orientation(const ab::ActorData& a){return {a.rotation.y,a.rotation.x};}
double seconds(){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return static_cast<double>(t.QuadPart)/static_cast<double>(f.QuadPart);}
bool enabled(const ab::Control& c,aster::EngineModule m){return (c.enabled&aster::moduleBit(m))!=0;}
bool attacking(const ab::Control& c){return c.mouse[(c.flags&ab::InvertClicks)?1:0]!=0;}
bool moving(const ab::Control& c){return (c.movement&15u)!=0||std::hypot(telemetry.local.velocity.x,telemetry.local.velocity.z)>.01f;}
bool permits(const ab::Control& c){return c.magic==ab::Magic&&c.protocol==ab::Protocol&&c.session==activeSession&&(!c.expectedWorld||c.expectedWorld==worldEpoch.load())&&!faulted.load()&&!stopping.load()&&!c.shutdown&&!c.probeOnly&&c.armed&&c.licensed&&GetTickCount64()<=c.heartbeat+3000&&worldVerified.load()&&localActor.load()&&captured.load()&&gameForeground()&&!(GetAsyncKeyState(VK_F9)&0x8000);}
template<class T>T* component(void* actor){
    if(!readable(actor,0x20))return nullptr;
    auto bytes=static_cast<unsigned char*>(actor);auto registry=*reinterpret_cast<const entt::basic_registry<EntityId>**>(bytes+0x10);auto entity=*reinterpret_cast<EntityId*>(bytes+0x18);
    if(!readable(registry,64)||!registry->valid(entity))return nullptr;
    auto pool=registry->storage(entt::type_hash<T>::value());if(!pool||!pool->contains(entity))return nullptr;
    auto value=pool->value(entity);return readable(value,sizeof(T))?static_cast<T*>(const_cast<void*>(value)):nullptr;
}
void restoreTimer(){
    if(ownedTimer&&writable(ownedTimer,sizeof(float))&&std::abs(*ownedTimer-lastTimer)<.001f)*ownedTimer=initialTimer;
    ownedTimer=nullptr;telemetry.timerApplied=20.f;
}
void timerControl(void* ci,const ab::Control& control){
    const bool apply=permits(control)&&(control.timerEnabled||enabled(control,aster::EngineModule::Timer))&&(!(control.flags&ab::TimerCombat)||attacking(control));
    auto& sample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Timer)];sample.conditions=apply;
    if(!apply){restoreTimer();return;}
    auto minecraft=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(ci)+0x1D0);
    if(!readable(minecraft,0xF0)){restoreTimer();return;}
    auto timer=*reinterpret_cast<float**>(minecraft+0xE8);
    if(!writable(timer,32)||!std::isfinite(timer[0])||timer[2]<-.1f||timer[2]>1.1f){restoreTimer();return;}
    if(ownedTimer&&ownedTimer!=timer)restoreTimer();
    if(!ownedTimer){if(std::abs(timer[0]-20.f)>.001f)return;ownedTimer=timer;initialTimer=timer[0];}
    if(std::abs(timer[0]-lastTimer)>.001f&&std::abs(timer[0]-initialTimer)>.001f){ownedTimer=nullptr;return;}
    lastTimer=20.f*std::clamp(control.timerMultiplier,.1f,5.f);timer[0]=lastTimer;telemetry.timerApplied=timer[0];
    sample.applied=1;++sample.requests;sample.appliedValue=timer[0];
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
    result.minimum=shape->minimum;result.maximum=shape->maximum;
    if(auto hurt=component<MobHurtTimeComponent>(const_cast<void*>(actor)))result.hurtTime=hurt->hurtTime;
    if(auto type=registry->try_get<ActorTypeComponent>(entity);readable(type,sizeof(*type)))result.type=type->type;
    if(auto dead=registry->storage<IsDeadFlagComponent>();dead&&dead->contains(entity))result.type=0;
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
bool collect(void* ci,bool probe){
    if(!readable(ci,0x300))return false;
    auto player=getPlayer(ci);telemetry.client=reinterpret_cast<std::uint64_t>(ci);telemetry.localPlayer=reinterpret_cast<std::uint64_t>(player);
    localActor.store(player);
    // Capture is observed from native transitions. Never construct/destroy game-owned STL strings in this DLL.
    strcpy_s(telemetry.screen,captured.load()?"gameplay_captured":"input_released");
    auto ciTable=*reinterpret_cast<std::uintptr_t**>(ci);
    if(readable(ciTable,sizeof(telemetry.clientVtable)))for(unsigned i=0;i<64;++i)telemetry.clientVtable[i]=ciTable[i]>=moduleBase&&ciTable[i]<moduleEnd?ciTable[i]-moduleBase:0;
    if(probe)timerCandidates(ci);else telemetry.timerCount=0;
    if(!player){
        worldVerified=false;telemetry.actorCount=0;regionPointer.store(nullptr);localGameMode.store(nullptr);
        if(lastWorldActor){telemetry.worldGeneration=++worldEpoch;lastWorldActor=lastWorldRegistry=0;lastWorldEntity=0;cachedAim=cachedRay=0;cameraRegistry.store(nullptr);ownedFov=nullptr;zoomWeight=0;for(auto& cached:blockNames)cached={};}
        telemetry.status=ab::Status::Hooked;return true;
    }
    telemetry.status=ab::Status::WorldDetected;
    if(!actorData(player,telemetry.local)){worldVerified=false;return false;}
    const auto registry=*reinterpret_cast<const entt::basic_registry<EntityId>* const*>(static_cast<unsigned char*>(player)+0x10);
    telemetry.actorCount=0;
    if(auto owners=registry->storage<ActorOwnerComponent>();owners){
        for(auto [entity,owner]:owners->each()){
            (void)entity;if(telemetry.actorCount>=ab::MaxActors)break;
            ab::ActorData data;if(actorData(owner.actor,data,registry))telemetry.actors[telemetry.actorCount++]=data;
        }
    }
    telemetry.gameMode=0;localGameMode.store(nullptr);
    for(unsigned offset=0xA00;offset<=0xB00;offset+=8){
        auto gm=*reinterpret_cast<void**>(static_cast<unsigned char*>(player)+offset);if(!readable(gm,32))continue;
        auto table=*reinterpret_cast<std::uintptr_t**>(gm);if(!readable(table,24*sizeof(void*)))continue;
        if(table[0]<moduleBase||table[0]>=moduleEnd)continue;
        // The GameMode owns a pointer back to the local player immediately after its vptr.
        if(*reinterpret_cast<void**>(static_cast<unsigned char*>(gm)+8)!=player)continue;
        telemetry.gameMode=reinterpret_cast<std::uint64_t>(gm);localGameMode.store(gm);
        for(unsigned i=0;i<24;++i)telemetry.gameModeVtable[i]=table[i]>=moduleBase&&table[i]<moduleEnd?table[i]-moduleBase:0;
        break;
    }
    if(lastWorldActor!=reinterpret_cast<std::uint64_t>(player)||lastWorldEntity!=telemetry.local.entityId||lastWorldRegistry!=reinterpret_cast<std::uint64_t>(registry)){
        lastWorldActor=reinterpret_cast<std::uint64_t>(player);lastWorldRegistry=reinterpret_cast<std::uint64_t>(registry);lastWorldEntity=static_cast<std::uint32_t>(telemetry.local.entityId);
        telemetry.worldGeneration=++worldEpoch;cachedAim=cachedRay=0;spinRemaining=0;for(auto& cached:blockNames)cached={};
    }
    telemetry.error=0;telemetry.status=ab::Status::LayoutVerified;worldVerified=true;return true;
}
struct BlockPos {int x,y,z;};
bool swing(void* player,unsigned source){if(!readable(player,8))return false;auto table=*reinterpret_cast<std::uintptr_t**>(player);return readable(table,110*sizeof(void*))&&table[109]==moduleBase+0x6DA6BB0&&reinterpret_cast<bool(__fastcall*)(void*,unsigned,unsigned)>(table[109])(player,source,0);}
BlockPos miningBlock{};void* miningMode=nullptr;std::uint64_t miningTick=0;
void stopMining(){if(miningMode&&miningMode==localGameMode.load()&&readable(miningMode,8)){auto table=*reinterpret_cast<std::uintptr_t**>(miningMode);if(readable(table,40)&&table[4]>=moduleBase&&table[4]<moduleEnd)reinterpret_cast<void(__fastcall*)(void*,const BlockPos&)>(table[4])(miningMode,miningBlock);}miningMode=nullptr;}
void mine(const ab::Control& c,BlockPos block,unsigned char face,const ab::ActorData& actor){
    if(!c.mining[0]||!telemetry.blockTarget||!enabled(c,aster::EngineModule::Clicker)){stopMining();return;}
    auto mode=localGameMode.load();if(!mode)return;auto table=*reinterpret_cast<std::uintptr_t**>(mode);bool destroyed=false;
    if(miningMode!=mode||block.x!=miningBlock.x||block.y!=miningBlock.y||block.z!=miningBlock.z){stopMining();miningMode=mode;miningBlock=block;miningTick=localTicks.load();reinterpret_cast<bool(__fastcall*)(void*,const BlockPos&,unsigned char,bool&)>(table[1])(mode,block,face,destroyed);}
    else if(miningTick!=localTicks.load()){miningTick=localTicks.load();swing(localActor.load(),2);reinterpret_cast<bool(__fastcall*)(void*,const BlockPos&,unsigned char,const ab::Float3&,bool&)>(table[3])(mode,block,face,actor.position,destroyed);}
}
bool blockName(void* region,BlockPos pos,char (&name)[64]){
    name[0]=0;if(!readable(region,8))return false;
    auto table=*reinterpret_cast<std::uintptr_t**>(region);if(!readable(table,24)||table[2]<moduleBase||table[2]>=moduleEnd)return false;
    auto block=reinterpret_cast<void*(__fastcall*)(void*,const BlockPos&)>(table[2])(region,pos);
    if(!readable(block,0x90))return false;
    // Block state objects are immutable. Cache their names, never block positions:
    // querying the source each time still observes placement/destruction immediately.
    for(const auto& cached:blockNames)if(cached.block==block){strcpy_s(name,cached.name);return true;}
    for(unsigned offset=0x20;offset<=0x80;offset+=8){
        auto legacy=*reinterpret_cast<unsigned char**>(static_cast<unsigned char*>(block)+offset);
        if(!readable(legacy,0x50))continue;
        auto text=legacy+8;auto size=*reinterpret_cast<std::size_t*>(text+16),capacity=*reinterpret_cast<std::size_t*>(text+24);
        if(size<5||size>60||capacity<size||capacity>1000)continue;
        auto chars=capacity<16?reinterpret_cast<char*>(text):*reinterpret_cast<char**>(text);
        if(!readable(chars,size)||memcmp(chars,"tile.",5))continue;
        memcpy(name,chars,size);name[size]=0;auto& cached=blockNames[blockNameNext++%128];cached.block=block;strcpy_s(cached.name,name);return true;
    }return false;
}
double blockRay(aster::Vec3 origin,aster::Vec3 direction,double maximum,BlockPos* hit=nullptr,unsigned char* face=nullptr){
    struct Timing {double start=seconds();~Timing(){rayMicros+=(seconds()-start)*1000000.;}} timing;
    auto region=regionPointer.load();if(!region)return 0;
    int cell[3]={static_cast<int>(std::floor(origin.x)),static_cast<int>(std::floor(origin.y)),static_cast<int>(std::floor(origin.z))};
    const double start[3]={origin.x,origin.y,origin.z},dir[3]={direction.x,direction.y,direction.z};
    double next[3]{},delta[3]{};int step[3]{};
    for(unsigned n=0;n<3;++n){step[n]=dir[n]>0?1:-1;delta[n]=std::abs(dir[n])<1e-12?1e100:std::abs(1./dir[n]);next[n]=std::abs(dir[n])<1e-12?1e100:(static_cast<double>(cell[n]+(step[n]>0?1:0))-start[n])/dir[n];}
    double distance=0;unsigned char entryFace=1;
    for(unsigned count=0;count<512&&distance<=maximum;++count){
        char name[64]{};if(!blockName(region,{cell[0],cell[1],cell[2]},name))return distance;
        const bool pass=!strcmp(name,"tile.air")||!strcmp(name,"tile.cave_air")||!strcmp(name,"tile.void_air")||strstr(name,"water")||strstr(name,"lava");
        if(!pass){if(hit)*hit={cell[0],cell[1],cell[2]};if(face)*face=entryFace;return distance;}
        unsigned axis=next[0]<=next[1]&&next[0]<=next[2]?0:next[1]<=next[2]?1:2;
        distance=next[axis];next[axis]+=delta[axis];cell[axis]+=step[axis];entryFace=axis==0?(step[axis]>0?4:5):axis==1?(step[axis]>0?0:1):(step[axis]>0?2:3);
    }return maximum+1;
}
bool visible(aster::Vec3 origin,aster::Vec3 target){aster::Vec3 delta{target.x-origin.x,target.y-origin.y,target.z-origin.z};auto length=aster::length(delta);if(length<1e-6)return true;return blockRay(origin,{delta.x/length,delta.y/length,delta.z/length},length)>=length-.001;}
void observeBlocks(void* ci,bool probe){
    auto table=*reinterpret_cast<std::uintptr_t**>(ci);
    auto region=reinterpret_cast<void*(__fastcall*)(void*)>(table[29])(ci);
    regionPointer.store(region);
    telemetry.region=reinterpret_cast<std::uint64_t>(region);
    if(!readable(region,8))return;
    auto regionTable=*reinterpret_cast<std::uintptr_t**>(region);
    if(!readable(regionTable,64))return;
    for(unsigned n=0;n<8;++n)telemetry.regionVtable[n]=regionTable[n]-moduleBase;
    if(regionTable[2]<moduleBase||regionTable[2]>=moduleEnd)return;
    if(!probe)return;
    for(int n=0;n<16;++n){
        BlockPos pos{static_cast<int>(std::floor(telemetry.local.position.x)),static_cast<int>(std::floor(telemetry.local.position.y))-12+n,static_cast<int>(std::floor(telemetry.local.position.z))};
        auto& sample=telemetry.blocks[n];sample.x=pos.x;sample.y=pos.y;sample.z=pos.z;sample.name[0]=0;
        blockName(region,pos,sample.name);
    }
}
void safeBlocks(void* ci,bool probe){__try{observeBlocks(ci,probe);}__except(EXCEPTION_EXECUTE_HANDLER){telemetry.error=3001;}}
void observeCameras(){
    telemetry.cameraCount=0;auto registry=cameraRegistry.load();if(!readable(registry,0x80))return;
    telemetry.cameraRegistry=reinterpret_cast<std::uint64_t>(registry);
    const char* names[]={"CurrentInputCameraComponent","DefaultInputCameraComponent","CameraComponent","CameraDirectLookComponent","CameraWorldSpaceRotationComponent","CameraLocalSpaceRotationComponent","CameraLookAtComponent","CameraClientInstanceComponent","RenderCameraComponent","GameCameraComponent","CameraTargetComponent","CameraAttachToEntityComponent","CameraCustomFovComponent"};
    for(auto name:names){
        const auto qualified=std::string("MinecraftCamera::")+name;auto hash=ab::componentHash(qualified.c_str());const auto* pool=registry->storage(hash);if(!pool)continue;
        for(auto entity:*pool){
            if(telemetry.cameraCount>=40)return;if(!registry->valid(entity))continue;
            auto data=pool->value(entity);
            auto& record=telemetry.cameras[telemetry.cameraCount++];record={};record.hash=hash;record.entity=static_cast<std::uint32_t>(entity);record.address=reinterpret_cast<std::uint64_t>(data);
            if(hash==ab::componentHash("MinecraftCamera::CameraComponent")){if(readable(data,96))memcpy(record.words+12,static_cast<const unsigned char*>(data)+48,48);}
            else {const auto bytes=hash==ab::componentHash("MinecraftCamera::CameraDirectLookComponent")?20u:hash==ab::componentHash("MinecraftCamera::CameraCustomFovComponent")?4u:0u;if(bytes&&readable(data,bytes))memcpy(record.words,data,bytes);}
        }
    }
}
void safeCameras(){__try{observeCameras();}__except(EXCEPTION_EXECUTE_HANDLER){telemetry.error=3002;}}
bool combatTarget(const ab::ActorData& a,const ab::Control& c){
    (void)c;return a.address!=telemetry.local.address&&a.entityId!=telemetry.local.entityId&&(a.type&0x100)!=0&&a.type!=317;
}
void* taggedCameraComponent(entt::id_type tagType,entt::id_type type,std::size_t bytes){
    auto registry=cameraRegistry.load();if(!readable(registry,0x80))return nullptr;
    auto tag=registry->storage(tagType);if(!tag||tag->empty())return nullptr;
    const auto entity=*tag->begin();if(!registry->valid(entity))return nullptr;
    auto pool=registry->storage(type);if(!pool||!pool->contains(entity))return nullptr;
    auto data=const_cast<void*>(pool->value(entity));return writable(data,bytes)?data:nullptr;
}
void* inputCameraComponent(entt::id_type type,std::size_t bytes){return taggedCameraComponent(ab::componentHash("MinecraftCamera::CurrentInputCameraComponent"),type,bytes);}
void restoreZoom(){if(ownedFov&&writable(ownedFov,4)&&std::abs(*ownedFov-lastFov)<.00001f)*ownedFov=initialFov;ownedFov=nullptr;zoomWeight=0;telemetry.zoomFactor=1;}
void zoomControl(const ab::Control& c,double dt){
    auto camera=static_cast<float*>(taggedCameraComponent(ab::componentHash("MinecraftCamera::RenderCameraComponent"),ab::componentHash("MinecraftCamera::CameraComponent"),96));
    if(!camera||!std::isfinite(camera[20])||camera[20]<.05f||camera[20]>3.13f){restoreZoom();return;}
    auto& sample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Zoom)];const bool on=enabled(c,aster::EngineModule::Zoom);sample.conditions=on;
    auto field=camera+20;if(field!=ownedFov){restoreZoom();if(!on)return;ownedFov=field;initialFov=*field;lastFov=*field;}
    if(std::abs(*field-lastFov)>.00001f)initialFov=*field; // Native camera settings may change between frames.
    const auto target=on?1.:0.;const auto duration=c.zoomSpeed<=0?0.:.65-.006*std::clamp(c.zoomSpeed,0.f,100.f);
    zoomWeight=duration==0?target:std::clamp(zoomWeight+(on?1.:-1.)*dt/duration,0.,1.);
    lastFov=initialFov*static_cast<float>(1.-.65*zoomWeight);*field=lastFov;telemetry.zoomFactor=static_cast<float>(1.-.65*zoomWeight);
    sample.applied=on&&zoomWeight>0;sample.appliedValue=lastFov;sample.observedValue=*field;if(sample.applied)++sample.requests;
    if(!on&&zoomWeight==0){ownedFov=nullptr;telemetry.zoomFactor=1;}
}
bool rotate(void*,double yaw,double pitch,bool horizontal,bool vertical){
    // The input camera owns persistent angles. ActorRotationComponent is a derived
    // value overwritten during the camera-to-actor update on this binary.
    auto look=static_cast<float*>(inputCameraComponent(ab::componentHash("MinecraftCamera::CameraDirectLookComponent"),20));
    if(!look)return false;
    if(!std::isfinite(look[0])||!std::isfinite(look[1])||look[3]<-1.58f||look[4]>1.58f||look[3]>=look[4])return false;
    constexpr auto radians=std::numbers::pi/180.;
    if(horizontal)look[0]=static_cast<float>(std::remainder(look[0]-yaw*radians,2.*std::numbers::pi));
    if(vertical)look[1]=std::clamp(look[1]-static_cast<float>(pitch*radians),look[3],look[4]);
    return true;
}
void features(void* ci,const ab::Control& c){
    const auto now=seconds();const auto dt=lastFrame?std::clamp(now-lastFrame,0.,.1):0.;lastFrame=now;
    for(auto& m:telemetry.modules){m.applied=0;m.conditions=0;}
    telemetry.aimYawDelta=telemetry.aimPitchDelta=0;telemetry.entityTarget=telemetry.blockTarget=0;telemetry.permitted=permits(c);
    if(!telemetry.permitted){restoreTimer();restoreZoom();stopMining();spinRemaining=spinPending=0;aimPending=spinMeasuring=spinFinished=false;spinAttack=attackCalls.load();lastPhysical=c.physicalPress[(c.flags&ab::InvertClicks)?1:0];for(unsigned i=0;i<2;++i)lastOutput[i]=c.outputClicks[i];return;}
    void* player=localActor.load();ab::ActorData local;if(!actorData(player,local))return;
    telemetry.local=local;auto origin=vec(local.position);auto view=orientation(local);auto direction=aster::viewDirection(view);
    if(aimPending){auto& sample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Aim)];sample.observedValue=static_cast<float>(view.yaw);if(std::hypot(aster::wrappedDegrees(view.yaw-aimExpected.yaw),view.pitch-aimExpected.pitch)<.1)++sample.observations;aimPending=false;}
    if(spinMeasuring){const auto change=aster::wrappedDegrees(view.yaw-spinPreviousYaw)*spinDirection;if(change>=-.1&&change<180)spinTravel+=change;spinPreviousYaw=view.yaw;if(spinFinished){auto& sample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Spin)];sample.observedValue=static_cast<float>(spinTravel);if(std::abs(spinTravel-360.)<1.)++sample.observations;spinMeasuring=spinFinished=false;}}
    const bool inCombat=attacking(c),inMotion=moving(c);
    const bool reach=enabled(c,aster::EngineModule::Reach)&&(!(c.flags&ab::ReachCombat)||inCombat)&&(!(c.flags&ab::ReachMoving)||inMotion);
    const bool hitbox=enabled(c,aster::EngineModule::Hitbox);
    const double range=reach?std::clamp(c.reach,3.f,10.f):3.;
    const bool needRay=enabled(c,aster::EngineModule::Reach)||enabled(c,aster::EngineModule::Hitbox)||enabled(c,aster::EngineModule::Clicker);
    BlockPos blockHit{};unsigned char blockFace=1;const auto blockDistance=needRay?blockRay(origin,direction,std::max(range,6.),&blockHit,&blockFace):std::max(range,6.)+1;telemetry.blockDistance=static_cast<float>(blockDistance);
    telemetry.blockX=blockHit.x;telemetry.blockY=blockHit.y;telemetry.blockZ=blockHit.z;
    telemetry.blockTarget=needRay&&blockDistance<=6.&&blockDistance>.001;
    mine(c,blockHit,blockFace,local);
    ab::ActorData* rayTarget=nullptr;double bestRay=range+1;
    ab::ActorData* aimTarget=nullptr;aster::Vec3 aimPoint{};double bestScore=1e100;
    const bool aim=enabled(c,aster::EngineModule::Aim)&&(!(c.flags&ab::AimCombat)||inCombat)&&(!(c.flags&ab::AimMoving)||inMotion);
    const double outer=std::clamp(c.aimOuter,0.f,static_cast<float>(std::numbers::pi))*180./std::numbers::pi;
    for(unsigned n=0;n<telemetry.actorCount;++n){
        auto& target=telemetry.actors[n];if(!combatTarget(target,c))continue;
        // Entity discovery is throttled, but dispatch and steering must never use
        // an actor that disappeared or whose address was recycled between updates.
        ab::ActorData live;const auto registry=*reinterpret_cast<const entt::basic_registry<EntityId>* const*>(static_cast<unsigned char*>(player)+0x10);
        if(!actorData(reinterpret_cast<void*>(target.address),live,registry)||live.entityId!=target.entityId)continue;
        target=live;if(!combatTarget(target,c))continue;
        const auto original=box(target);const auto expanded=hitbox?aster::targetingBox(original,c.boxWidth):original;
        if(auto distance=needRay?aster::rayBox(origin,direction,expanded,range):std::nullopt;distance&&*distance<blockDistance-.001){
            bool prefer=!(c.flags&ab::BoxDeterministic)&&target.entityId==cachedRay;
            if(prefer||(!rayTarget||(!(c.flags&ab::BoxDeterministic)&&rayTarget->entityId==cachedRay?false:*distance<bestRay-1e-6||(*distance<=bestRay+1e-6&&target.entityId<rayTarget->entityId)))){rayTarget=&target;bestRay=*distance;}
        }
        if(!aim||((c.flags&ab::AimPlayers)&&target.type!=319))continue;
        aster::Vec3 point{(original.minimum.x+original.maximum.x)*.5,(original.minimum.y+original.maximum.y)*.5+c.aimHeight,(original.minimum.z+original.maximum.z)*.5};
        const double distance=aster::length({point.x-origin.x,point.y-origin.y,point.z-origin.z});if(distance>c.aimDistance||distance<.01)continue;
        auto targetView=aster::lookAt(origin,point);const double yaw=(c.flags&ab::AimHorizontal)?aster::wrappedDegrees(targetView.yaw-view.yaw):0,pitch=(c.flags&ab::AimVertical)?targetView.pitch-view.pitch:0;
        const double score=std::hypot(yaw,pitch);if(score>outer)continue;
        if((c.flags&ab::AimWalls)&&!visible(origin,point))continue;
        const bool prefer=(c.flags&ab::AimCache)&&target.entityId==cachedAim;
        if(prefer||(!aimTarget||((c.flags&ab::AimCache)&&aimTarget->entityId==cachedAim?false:score<bestScore-1e-6||(score<=bestScore+1e-6&&target.entityId<aimTarget->entityId)))){aimTarget=&target;aimPoint=point;bestScore=score;}
    }
    cachedRay=rayTarget?rayTarget->entityId:0;cachedAim=aimTarget?aimTarget->entityId:0;
    telemetry.entityTarget=rayTarget!=nullptr;telemetry.selectedTarget=rayTarget?rayTarget->entityId:aimTarget?aimTarget->entityId:0;telemetry.rayDistance=rayTarget?static_cast<float>(bestRay):0;
    auto& reachSample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Reach)];
    auto& boxSample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Hitbox)];
    reachSample.conditions=reach;boxSample.conditions=hitbox;
    if(reach&&rayTarget)reachSample.appliedValue=static_cast<float>(range);
    if(hitbox&&rayTarget)boxSample.appliedValue=c.boxWidth;
    const auto presses=c.physicalPress[(c.flags&ab::InvertClicks)?1:0];const bool physicalClick=presses!=lastPhysical;lastPhysical=presses;
    std::uint64_t queued[2]{};for(unsigned i=0;i<2;++i){queued[i]=c.outputClicks[i]>=lastOutput[i]?std::min<std::uint64_t>(c.outputClicks[i]-lastOutput[i],4):0;lastOutput[i]=c.outputClicks[i];}
    auto& clicks=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Clicker)];clicks.conditions=enabled(c,aster::EngineModule::Clicker);
    const auto normal=rayTarget?aster::rayBox(origin,direction,box(*rayTarget),3.):std::nullopt;
    unsigned left=clicks.conditions?static_cast<unsigned>(queued[0]):0;
    if(physicalClick&&rayTarget&&!normal&&(reach||hitbox))++left;
    if(telemetry.gameMode){
        auto mode=reinterpret_cast<void*>(telemetry.gameMode);
        for(unsigned i=0;i<left;++i){
            if(clicks.conditions){++clicks.requests;++telemetry.clickRequests[0];}
            const bool swung=swing(player,telemetry.blockTarget&&!rayTarget?2:4);
            if(rayTarget){
                if(reach&&!normal){++reachSample.requests;reachSample.applied=1;}if(hitbox&&!normal){++boxSample.requests;boxSample.applied=1;}
                lastDispatchTime=GetTickCount64();lastDispatchTarget=rayTarget->entityId;
                const auto contact=packed({origin.x+direction.x*bestRay,origin.y+direction.y*bestRay,origin.z+direction.z*bestRay});
                const bool accepted=reinterpret_cast<AttackFn>(moduleBase+telemetry.gameModeVtable[15])(mode,reinterpret_cast<void*>(rayTarget->address),contact);
                if(clicks.conditions&&accepted){++clicks.observations;++telemetry.clickActions[0];clicks.applied=1;}
            }else if(telemetry.blockTarget){
                bool destroyed=false;const bool accepted=reinterpret_cast<bool(__fastcall*)(void*,const BlockPos&,unsigned char,bool&)>(moduleBase+telemetry.gameModeVtable[1])(mode,blockHit,blockFace,destroyed);
                if(accepted){++clicks.observations;++telemetry.clickActions[0];clicks.applied=1;}
            }else if(swung){++clicks.observations;++telemetry.clickActions[0];clicks.applied=1;}
        }
        if(clicks.conditions)for(std::uint64_t i=0;i<queued[1];++i){
            ++clicks.requests;++telemetry.clickRequests[1];bool accepted=false;
            if(rayTarget){const auto contact=packed({origin.x+direction.x*bestRay,origin.y+direction.y*bestRay,origin.z+direction.z*bestRay});accepted=reinterpret_cast<bool(__fastcall*)(void*,void*,const ab::Float3&,unsigned char)>(moduleBase+telemetry.gameModeVtable[14])(mode,reinterpret_cast<void*>(rayTarget->address),contact,0);}
            else if(telemetry.blockTarget){const auto before=buildAccepted.load();reinterpret_cast<void(__fastcall*)(void*,const BlockPos&,unsigned char,unsigned char)>(moduleBase+telemetry.gameModeVtable[5])(mode,blockHit,blockFace,0);accepted=buildAccepted.load()!=before;}
            else {auto table=*reinterpret_cast<std::uintptr_t**>(player);if(readable(table,0x270)&&table[77]>=moduleBase&&table[77]<moduleEnd){auto item=reinterpret_cast<void*(__fastcall*)(void*)>(table[77])(player);if(readable(item,16))accepted=reinterpret_cast<bool(__fastcall*)(void*,void*,unsigned char)>(moduleBase+telemetry.gameModeVtable[11])(mode,item,0);}}
            if(accepted){++clicks.observations;++telemetry.clickActions[1];clicks.applied=1;}
        }
    }
    // Hurt state is sampled independently from attack dispatch; it is not inferred from SendInput.
    static std::uint64_t previousTarget=0;static int previousHurt=0;
    if(rayTarget){if(previousTarget==rayTarget->entityId&&rayTarget->hurtTime>previousHurt&&GetTickCount64()-lastDispatchTime<1000&&lastDispatchTarget==rayTarget->entityId){if(reach)++reachSample.observations;if(hitbox)++boxSample.observations;}previousTarget=rayTarget->entityId;previousHurt=rayTarget->hurtTime;}
    else {previousTarget=0;previousHurt=0;}
    auto& spin=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Spin)];
    const auto attacks=attackCalls.load();
    if(enabled(c,aster::EngineModule::Spin)&&(c.spinLeft+c.spinRight)>0){
        spin.conditions=spinRemaining>0||attacks!=spinAttack;
        if(spinRemaining<=0&&attacks!=spinAttack&&now>=nextSpin){spinRemaining=360.;spinPending=0;const auto leftWeight=std::clamp(c.spinLeft,0.f,100.f),rightWeight=std::clamp(c.spinRight,0.f,100.f);spinDirection=std::uniform_real_distribution<float>(0,leftWeight+rightWeight)(randomDirection)<leftWeight?-1:1;spinPreviousYaw=view.yaw;spinTravel=0;spinMeasuring=true;}
        if(spinRemaining>0){
            spinPending+=std::clamp(c.spinSpeed,.1f,10.f)*360.*dt;
            if(spinPending>=std::max(.00036,static_cast<double>(c.spinStep)*360.)||spinPending>=spinRemaining){
                const auto delta=std::min(spinPending,spinRemaining);if(rotate(player,delta*spinDirection,0,true,false)){spinRemaining-=delta;spinPending=0;spin.applied=1;++spin.requests;spin.appliedValue=static_cast<float>(360.-spinRemaining);}
                if(spinRemaining<1e-5){spinRemaining=0;spinFinished=true;nextSpin=now+std::clamp(c.spinInterval,0.f,10000.f)/1000.;}
            }
        }
    }else spinRemaining=spinPending=0;
    spinAttack=attacks;
    auto& aimSample=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Aim)];aimSample.conditions=aim&&aimTarget&&spinRemaining<=0;
    if(aimSample.conditions){
        const bool onTarget=aster::rayBox(origin,direction,box(*aimTarget),c.aimDistance).has_value();
        if(!(c.flags&ab::AimOnTarget)||!onTarget){
            auto desired=aster::lookAt(origin,aimPoint);auto corrected=aster::aimCorrection(view,desired,c.aimSmooth,(c.flags&ab::AimBlatant)!=0,dt,(c.flags&ab::AimHorizontal)!=0,(c.flags&ab::AimVertical)!=0,c.aimInner);
            const auto yaw=aster::wrappedDegrees(corrected.yaw-view.yaw),pitch=corrected.pitch-view.pitch;
            if(std::hypot(yaw,pitch)>1e-7&&rotate(player,yaw,pitch,(c.flags&ab::AimHorizontal)!=0,(c.flags&ab::AimVertical)!=0)){telemetry.aimYawDelta=static_cast<float>(yaw);telemetry.aimPitchDelta=static_cast<float>(pitch);aimSample.applied=1;++aimSample.requests;aimSample.appliedValue=static_cast<float>(yaw);aimExpected=corrected;aimPending=true;}
        }
    }
    auto& velocity=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Velocity)];velocity.conditions=enabled(c,aster::EngineModule::Velocity)&&(!(c.flags&ab::VelocityCombat)||inCombat);velocity.requests=velocityChanges.load();velocity.observations=velocityObservations.load();
    {std::lock_guard lock(velocityMutex);telemetry.velocityBefore=velocityBefore;telemetry.velocityAfter=velocityAfter;telemetry.motionSequence=motionSequence;memcpy(telemetry.motions,motionEvents,sizeof(motionEvents));}
    velocity.appliedValue=c.velocityHorizontal;velocity.observedValue=std::hypot(telemetry.velocityAfter.x,telemetry.velocityAfter.z);velocity.applied=velocity.conditions&&velocityChanges.load()>0&&GetTickCount64()-velocityLastApplied.load()<100;
    timerControl(ci,c);
    zoomControl(c,dt);
}
int featureException(EXCEPTION_POINTERS* info){telemetry.exceptionRva=reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress)-moduleBase;if(info->ExceptionRecord->NumberParameters>=2)telemetry.exceptionAddress=info->ExceptionRecord->ExceptionInformation[1];return EXCEPTION_EXECUTE_HANDLER;}
void safeFeatures(void* ci,const ab::Control* c){__try{features(ci,*c);}__except(featureException(GetExceptionInformation())){faulted=true;++telemetry.layoutFailures;telemetry.status=ab::Status::LayoutFailure;telemetry.error=4001;restoreTimer();}}
ab::Float3* prepareMotionPacket(void* packetOwner,ab::MotionEvent* event){
    if(!readable(packetOwner,16))return nullptr;auto packet=*reinterpret_cast<unsigned char**>(packetOwner);
    if(!writable(packet,0x58)||*reinterpret_cast<std::uintptr_t*>(packet)!=moduleBase+0xF0B2C40)return nullptr;
    auto player=localActor.load();auto runtime=component<RuntimeIDComponent>(player);
    if(!runtime||*reinterpret_cast<std::uint64_t*>(packet+0x30)!=runtime->id)return nullptr;
    auto field=reinterpret_cast<ab::Float3*>(packet+0x38);if(!finite(*field)||aster::length(vec(*field))<.001)return nullptr;
    auto state=component<StateVectorComponent>(player);auto rotation=component<ActorRotationComponent>(player);if(!state||!rotation)return nullptr;
    FILETIME filetime;GetSystemTimePreciseAsFileTime(&filetime);ULARGE_INTEGER time;time.LowPart=filetime.dwLowDateTime;time.HighPart=filetime.dwHighDateTime;event->wall=time.QuadPart/10000-11644473600000ull;
    event->previous=state->velocity;event->incoming=event->requested=*field;
    ab::Control c;if(shared&&ab::readConsistent(shared->controlSequence,shared->control,c)){
        event->horizontal=c.velocityHorizontal;event->vertical=c.velocityVertical;event->flags=c.flags;
        if(permits(c)&&enabled(c,aster::EngineModule::Velocity)&&(!(c.flags&ab::VelocityCombat)||attacking(c))){
            const auto result=aster::scaledImpulse(vec(state->velocity),vec(*field),c.velocityHorizontal,c.velocityVertical,(c.flags&ab::VelocityCombine)!=0,(c.flags&ab::VelocityNoBackwards)!=0,rotation->rotation.y);
            if(aster::length(result)<=100){event->requested=packed(result);event->scaled=1;*field=event->requested;}
        }
    }return field;
}
ab::Float3* safePrepareMotionPacket(void* packetOwner,ab::MotionEvent* event){__try{return prepareMotionPacket(packetOwner,event);}__except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}}
void finishMotionPacket(void* packetOwner,ab::Float3* field,ab::MotionEvent* event){
    auto packet=reinterpret_cast<unsigned char*>(field)-0x38;
    if(event->scaled&&readable(packetOwner,8)&&*reinterpret_cast<void**>(packetOwner)==packet&&writable(field,sizeof(*field))&&!memcmp(field,&event->requested,sizeof(*field)))*field=event->incoming;
    if(auto state=component<StateVectorComponent>(localActor.load());state&&finite(state->velocity)){event->observed=state->velocity;event->observedValid=1;}
}
void safeFinishMotionPacket(void* owner,ab::Float3* field,ab::MotionEvent* event){__try{finishMotionPacket(owner,field,event);}__except(EXCEPTION_EXECUTE_HANDLER){}}
void __fastcall receiveMotionHook(void* dispatcher,void* source,void* callback,void* packetOwner){
    CallbackLifetime lifetime;ab::MotionEvent event;auto field=safePrepareMotionPacket(packetOwner,&event);
    // The original dispatcher copies the velocity into native movement state.
    // Borrow the packet for this call only; no game-owned STL object is copied.
    originalReceive(dispatcher,source,callback,packetOwner);
    if(field){safeFinishMotionPacket(packetOwner,field,&event);std::lock_guard lock(velocityMutex);event.sequence=++motionSequence;motionEvents[(motionSequence-1)%32]=event;if(event.scaled){velocityBefore=event.incoming;velocityAfter=event.requested;++velocityChanges;velocityLastApplied=GetTickCount64();if(event.observedValid&&aster::length({event.observed.x-event.requested.x,event.observed.y-event.requested.y,event.observed.z-event.requested.z})<1e-5)++velocityObservations;}}
}
void __fastcall grabHook(void* ci){CallbackLifetime lifetime;originalGrab(ci);captured=true;}
void __fastcall releaseHook(void* ci){CallbackLifetime lifetime;captured=false;originalRelease(ci);}
bool __fastcall buildHook(void* mode,const BlockPos& block,unsigned char face,unsigned char hand,bool simTick){CallbackLifetime lifetime;const bool accepted=originalBuild(mode,block,face,hand,simTick);if(accepted&&mode==localGameMode.load())++buildAccepted;return accepted;}
bool safeCollect(void* ci,bool probe){__try{return collect(ci,probe);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
bool localPlayerChanged(void* ci){__try{return getPlayer(ci)!=localActor.load();}__except(EXCEPTION_EXECUTE_HANDLER){return true;}}
void safeTimerControl(void* ci,const ab::Control* control){__try{timerControl(ci,*control);}__except(EXCEPTION_EXECUTE_HANDLER){ownedTimer=nullptr;telemetry.error=2001;}}
void __fastcall tickHook(void* actor){CallbackLifetime lifetime;originalTick(actor);++allTicks;if(actor==localActor.load())++localTicks;}
std::uint64_t __fastcall updateHook(void* ci,std::uint64_t initialized,std::uint64_t allowUpdate,std::uint64_t fourth){
    CallbackLifetime lifetime;auto result=originalUpdate(ci,initialized,allowUpdate,fourth);client.store(ci);
    if(!collecting.test_and_set(std::memory_order_acquire)){
        const auto workStart=seconds();static double workTotal=0,workMax=0,workPeriod=workStart,collectTotal=0,rayTotal=0;static unsigned workCount=0;rayMicros=0;
        ++telemetry.updateCalls;auto now=GetTickCount64();
        ab::Control control;const bool hasControl=shared&&ab::readConsistent(shared->controlSequence,shared->control,control);
        static std::uint64_t lastCollect=0;
        if(shared&&(now-lastCollect>=30||localPlayerChanged(ci))){
            lastCollect=now;
            const auto collectStart=seconds();if(!faulted.load()&&!safeCollect(ci,hasControl&&control.probeOnly)){faulted=true;worldVerified=false;++telemetry.layoutFailures;telemetry.status=ab::Status::LayoutFailure;}collectTotal+=(seconds()-collectStart)*1000000.;
            if(telemetry.status==ab::Status::LayoutVerified)safeBlocks(ci,hasControl&&control.probeOnly);
            if(hasControl&&control.probeOnly&&telemetry.status==ab::Status::LayoutVerified)safeCameras();
        }
        if(hasControl)safeFeatures(ci,&control);
        const auto workEnd=seconds(),workMicros=(workEnd-workStart)*1000000.;workTotal+=workMicros;workMax=std::max(workMax,workMicros);rayTotal+=rayMicros;++workCount;
        if(workEnd-workPeriod>=1.){telemetry.adapterMeanMicros=static_cast<float>(workTotal/workCount);telemetry.adapterMaxMicros=static_cast<float>(workMax);telemetry.adapterCollectMicros=static_cast<float>(collectTotal/workCount);telemetry.adapterRayMicros=static_cast<float>(rayTotal/workCount);telemetry.adapterUpdateHz=static_cast<float>(workCount/(workEnd-workPeriod));workTotal=workMax=collectTotal=rayTotal=0;workCount=0;workPeriod=workEnd;}
        if(shared&&now-telemetry.heartbeat>=50){
            telemetry.heartbeat=now;
            telemetry.attackCalls=attackCalls.load();telemetry.attackTarget=attackTarget.load();telemetry.attackArgument=attackArgument.load();
            {std::lock_guard lock(attackMutex);memcpy(telemetry.attackArguments,attackObservation.arguments,sizeof(telemetry.attackArguments));memcpy(telemetry.attackTargetWords,attackObservation.target,sizeof(telemetry.attackTargetWords));memcpy(telemetry.attackExtraWords,attackObservation.extra,sizeof(telemetry.attackExtraWords));}
            telemetry.localTicks=localTicks.load();
            telemetry.allActorTicks=allTicks.load();telemetry.foreground=gameForeground()?1u:0u;
            static auto ticksAtStart=localTicks.load();static auto periodStart=now;
            if(now-periodStart>=1000){telemetry.measuredLocalTps=static_cast<float>((localTicks.load()-ticksAtStart)*1000.)/static_cast<float>(now-periodStart);ticksAtStart=localTicks.load();periodStart=now;
                auto& timer=telemetry.modules[static_cast<unsigned>(aster::EngineModule::Timer)];timer.observedValue=telemetry.measuredLocalTps;if(timer.applied&&std::abs(timer.observedValue-timer.appliedValue)<std::max(2.f,timer.appliedValue*.1f))++timer.observations;}
            ab::writeConsistent(shared->telemetrySequence,shared->telemetry,telemetry);
        }
        collecting.clear(std::memory_order_release);
    }
    return result;
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
    activeSession=control.session;
    moduleBase=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(moduleBase+dos->e_lfanew);moduleEnd=moduleBase+nt->OptionalHeader.SizeOfImage;
    auto update=scanUnique("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? FE FF FF FF 45 89 C6 89 D3 48 89 CE 48 8B 01 48 8B 80 ? ? ? ? 48 8D 95 ? ? ? ? FF 15");
    auto player=scanUnique("48 83 EC ? 48 8B 05 ? ? ? ? 48 31 E0 48 89 44 24 ? 48 8D 91 68 02 00 00 48 8D 4C 24 ? E8 ? ? ? ? 80 7C 24 ? 01 75 ? 48 8D 4C 24 ? B2 01");
    auto tick=scanUnique("55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ? ? ? ? 48 8D AC 24 ? ? ? ? 44 0F 29 85 ? ? ? ? 66 0F 7F BD ? ? ? ? 0F 29 B5 ? ? ? ? 48 C7 85 ? ? ? ? ? ? ? ? 48 89 CE 80 B9 ? ? ? ? 01 75 ? 83 BE ? ? ? ? 00 7F ? 48 8B 8E ? ? ? ? 48 8B 01");
    getPlayer=reinterpret_cast<PlayerFn>(player);
    bool installed=update&&player&&MH_Initialize()==MH_OK&&MH_CreateHook(reinterpret_cast<void*>(update),updateHook,reinterpret_cast<void**>(&originalUpdate))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(update))==MH_OK;
    bool tickInstalled=installed&&tick&&MH_CreateHook(reinterpret_cast<void*>(tick),tickHook,reinterpret_cast<void**>(&originalTick))==MH_OK&&MH_EnableHook(reinterpret_cast<void*>(tick))==MH_OK;
    auto motion=reinterpret_cast<void*>(moduleBase+0x23A7600);
    bool motionInstalled=installed&&MH_CreateHook(motion,receiveMotionHook,reinterpret_cast<void**>(&originalReceive))==MH_OK&&MH_EnableHook(motion)==MH_OK;
    auto build=reinterpret_cast<void*>(moduleBase+0x376B290);
    bool buildInstalled=installed&&MH_CreateHook(build,buildHook,reinterpret_cast<void**>(&originalBuild))==MH_OK&&MH_EnableHook(build)==MH_OK;
    auto attack=reinterpret_cast<void*>(moduleBase+0x3769350);
    bool attackInstalled=installed&&MH_CreateHook(attack,AsterAttackObserverEntry,&AsterAttackTrampoline)==MH_OK&&MH_EnableHook(attack)==MH_OK;
    originalAttack=reinterpret_cast<AttackFn>(AsterAttackTrampoline);
    auto camera=reinterpret_cast<void*>(moduleBase+0xAC2290);
    bool cameraInstalled=installed&&MH_CreateHook(camera,AsterCameraObserverEntry,&AsterCameraTrampoline)==MH_OK&&MH_EnableHook(camera)==MH_OK;
    auto grab=reinterpret_cast<void*>(moduleBase+0x65BB0A0),release=reinterpret_cast<void*>(moduleBase+0x65BB0F0);
    bool grabInstalled=installed&&MH_CreateHook(grab,grabHook,reinterpret_cast<void**>(&originalGrab))==MH_OK&&MH_EnableHook(grab)==MH_OK;
    bool releaseInstalled=installed&&MH_CreateHook(release,releaseHook,reinterpret_cast<void**>(&originalRelease))==MH_OK&&MH_EnableHook(release)==MH_OK;
    telemetry.boundMask=installed?((1u<<aster::EngineModuleCount)-1u):0u;
    if(!tickInstalled)telemetry.boundMask&=~(aster::moduleBit(aster::EngineModule::Timer)|aster::moduleBit(aster::EngineModule::Velocity));
    if(!motionInstalled)telemetry.boundMask&=~aster::moduleBit(aster::EngineModule::Velocity);
    if(!cameraInstalled)telemetry.boundMask&=~(aster::moduleBit(aster::EngineModule::Aim)|aster::moduleBit(aster::EngineModule::Zoom)|aster::moduleBit(aster::EngineModule::Spin));
    if(!buildInstalled)telemetry.boundMask&=~aster::moduleBit(aster::EngineModule::Clicker);
    if(!attackInstalled)telemetry.boundMask&=~(aster::moduleBit(aster::EngineModule::Reach)|aster::moduleBit(aster::EngineModule::Hitbox)|aster::moduleBit(aster::EngineModule::STap));
    if(!grabInstalled||!releaseInstalled)telemetry.boundMask=0;
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
        if(attackInstalled)MH_DisableHook(attack);
        if(cameraInstalled)MH_DisableHook(camera);
        if(motionInstalled)MH_DisableHook(motion);
        if(buildInstalled)MH_DisableHook(build);
        if(grabInstalled)MH_DisableHook(grab);if(releaseInstalled)MH_DisableHook(release);
        auto until=GetTickCount64()+2000;while(activeCalls&&GetTickCount64()<until)Sleep(10);
        if(activeCalls)return 0; // Keep the disabled DLL resident rather than unloading under a live callback.
        Sleep(50); // Disabled detours cannot re-enter; allow completed callback epilogues to leave this image.
        restoreTimer();restoreZoom();MH_RemoveHook(reinterpret_cast<void*>(update));if(tickInstalled)MH_RemoveHook(reinterpret_cast<void*>(tick));MH_Uninitialize();
    }
    telemetry.status=ab::Status::Stopped;ab::writeConsistent(shared->telemetrySequence,shared->telemetry,telemetry);
    UnmapViewOfFile(shared);shared=nullptr;CloseHandle(mapping);CloseHandle(file);FreeLibraryAndExitThread(selfModule,0);
}
}
void observeCameraRaw(void* registry,std::uint64_t hash){
    if(hash==0xF6C60B46&&readable(registry,0x80))cameraRegistry.store(static_cast<const entt::basic_registry<EntityId>*>(registry));
}
extern "C" void AsterObserveCamera(void* registry,void* hash,void*,void*){CallbackLifetime lifetime;observeCameraRaw(registry,reinterpret_cast<std::uint64_t>(hash));}
void observeAttackRaw(void* mode,void* target,std::uint64_t extra,std::uint64_t fourth){
    if(mode!=localGameMode.load())return;
    ++attackCalls;attackTarget=reinterpret_cast<std::uint64_t>(target);attackArgument=extra;
    AttackObservation observation;observation.arguments[0]=reinterpret_cast<std::uint64_t>(mode);observation.arguments[1]=reinterpret_cast<std::uint64_t>(target);observation.arguments[2]=extra;observation.arguments[3]=fourth;
    if(readable(target,sizeof(observation.target)))memcpy(observation.target,target,sizeof(observation.target));if(readable(reinterpret_cast<void*>(fourth),sizeof(ab::Float3)))memcpy(observation.extra,reinterpret_cast<void*>(fourth),sizeof(ab::Float3));
    {std::lock_guard lock(attackMutex);attackObservation=observation;}
}
extern "C" void AsterObserveAttack(void* mode,void* target,std::uint64_t extra,std::uint64_t fourth){CallbackLifetime lifetime;observeAttackRaw(mode,target,extra,fourth);}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){selfModule=module;DisableThreadLibraryCalls(module);if(auto thread=CreateThread(nullptr,0,worker,nullptr,0,nullptr))CloseHandle(thread);}
    return TRUE;
}
