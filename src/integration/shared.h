#pragma once
#include <Windows.h>
#include <cstdint>
#include <type_traits>
#include "core/runtime.h"
namespace aster::bridge {
inline constexpr std::uint32_t Protocol=8;
inline constexpr std::uint32_t Magic=0x41535452;
inline constexpr unsigned MaxActors=128;
struct Float3 {float x=0,y=0,z=0;};
struct Float2 {float x=0,y=0;};
enum class Status:std::uint32_t {Waiting,Hooked,WorldDetected,LayoutVerified,Stopped,SignatureFailure,LayoutFailure};
enum Flag:std::uint32_t {
    ReachCombat=1u,ReachMoving=2u,AimVertical=4u,AimHorizontal=8u,AimPlayers=16u,AimMoving=32u,
    AimWalls=64u,AimOnTarget=128u,AimCache=256u,AimCombat=512u,AimBlatant=1024u,
    BoxDeterministic=2048u,VelocityCombat=4096u,VelocityCombine=8192u,VelocityNoBackwards=16384u,
    TimerCombat=32768u,InvertClicks=65536u,ZoomLowCpu=131072u
};
struct Control {
    std::uint32_t magic=Magic,protocol=Protocol;
    std::uint64_t session=0,heartbeat=0,expectedWorld=0;
    std::uint32_t shutdown=0,probeOnly=1;
    float timerMultiplier=1.f;
    std::uint32_t timerEnabled=0;
    std::uint32_t enabled=0,flags=0,armed=0,licensed=1,mouse[2]{},movement=0,mining[2]{};
    std::uint64_t physicalPress[2]{},outputClicks[2]{};
    float reach=5.2f,boxWidth=2.22f,aimSmooth=25.f,aimOuter=.4f,aimInner=.02f,aimDistance=5.f,aimHeight=-.3f;
    float velocityHorizontal=.9255f,velocityVertical=.9255f;
    float spinSpeed=2.f,spinLeft=50.f,spinRight=50.f,spinStep=.0001f,spinInterval=0.f,zoomSpeed=0.f;
};
struct ActorData {
    std::uint64_t entityId=0,address=0;
    Float3 position,velocity;
    Float2 rotation;
    float width=0,height=0;
    std::int32_t type=0;
    Float3 minimum,maximum;
    std::int32_t hurtTime=0;
};
struct TimerCandidate {std::uint32_t clientOffset=0,minecraftOffset=0;std::uint64_t address=0;float data[8]{};};
struct MotionEvent {
    std::uint64_t sequence=0,wall=0;
    std::uint32_t scaled=0,flags=0,observedValid=0;
    Float3 previous,incoming,requested,observed;
    float horizontal=1,vertical=1;
};
struct Telemetry {
    std::uint32_t magic=Magic,protocol=Protocol;
    Status status=Status::Waiting;
    std::uint32_t error=0;
    std::uint64_t heartbeat=0,updateCalls=0,client=0,localPlayer=0;
    float adapterMeanMicros=0,adapterMaxMicros=0,adapterUpdateHz=0,adapterCollectMicros=0,adapterRayMicros=0;
    std::uint64_t localTicks=0;
    float measuredLocalTps=0,timerApplied=20.f;
    std::uint32_t foreground=0;
    std::uint64_t allActorTicks=0;
    ActorData local;
    std::uint32_t actorCount=0,layoutFailures=0;
    ActorData actors[MaxActors]{};
    std::uint32_t timerCount=0;
    TimerCandidate timers[12]{};
    std::uint64_t gameMode=0;
    std::uint64_t gameModeVtable[24]{};
    std::uint64_t clientVtable[64]{};
    char screen[64]{};
    std::uint64_t attackCalls=0,attackTarget=0,attackArgument=0;
    std::uint64_t region=0,regionVtable[8]{};
    struct BlockSample {std::int32_t x=0,y=0,z=0;char name[64]{};} blocks[16]{};
    std::uint32_t boundMask=0,permitted=0,gate=0,blockTarget=0,entityTarget=0;
    std::uint64_t worldGeneration=0,selectedTarget=0;
    float blockDistance=0,rayDistance=0,aimYawDelta=0,aimPitchDelta=0,zoomFactor=1.f;
    Float3 velocityBefore,velocityAfter;
    std::int32_t blockX=0,blockY=0,blockZ=0;
    std::uint32_t cameraCount=0;
    std::uint64_t exceptionRva=0,exceptionAddress=0;
    struct CameraRecord {std::uint32_t hash=0,entity=0;std::uint64_t address=0;std::uint32_t words[24]{};} cameras[40]{};
    std::uint64_t attackArguments[4]{},cameraRegistry=0;
    std::uint64_t clickRequests[2]{},clickActions[2]{};
    std::uint64_t motionSequence=0;
    MotionEvent motions[32]{};
    std::uint32_t attackTargetWords[16]{},attackExtraWords[16]{};
    struct ModuleSample {std::uint32_t conditions=0,applied=0;std::uint64_t requests=0,observations=0;float appliedValue=0,observedValue=0;} modules[EngineModuleCount]{};
};
struct Shared {
    volatile LONG controlSequence=0;
    Control control;
    volatile LONG telemetrySequence=0;
    Telemetry telemetry;
};
static_assert(std::is_trivially_copyable_v<Control>&&std::is_trivially_copyable_v<Telemetry>);
template<class T> bool readConsistent(const volatile LONG& sequence,const T& source,T& result){
    auto before=InterlockedCompareExchange(const_cast<volatile LONG*>(&sequence),0,0);if(before&1)return false;
    MemoryBarrier();memcpy(&result,&source,sizeof(T));MemoryBarrier();
    return before==InterlockedCompareExchange(const_cast<volatile LONG*>(&sequence),0,0);
}
template<class T> void writeConsistent(volatile LONG& sequence,T& destination,const T& source){
    InterlockedIncrement(&sequence);MemoryBarrier();memcpy(&destination,&source,sizeof(T));MemoryBarrier();InterlockedIncrement(&sequence);
}
} // namespace aster::bridge
