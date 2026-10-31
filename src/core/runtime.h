#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
namespace aster {
enum class EngineModule:unsigned {Reach,Clicker,Aim,Hitbox,Velocity,Timer,Zoom,Spin,Sprint,STap,Walk,Count};
inline constexpr unsigned EngineModuleCount=static_cast<unsigned>(EngineModule::Count);
inline constexpr std::array<std::string_view,EngineModuleCount> EngineModuleIds{
    "reach","clicker","aim","hitbox","velocity","timer","zoom","auto360","sprint","stap","walk"};
constexpr unsigned moduleIndex(std::string_view id){for(unsigned n=0;n<EngineModuleCount;++n)if(EngineModuleIds[n]==id)return n;return EngineModuleCount;}
constexpr std::uint32_t moduleBit(EngineModule m){return 1u<<static_cast<unsigned>(m);}
struct ModuleEvidence {
    bool bound=false,conditions=false,requested=false,applied=false;
    std::uint64_t requests=0,observations=0;
    double appliedValue=0,observedValue=0;
    std::string reason;
};
struct EngineRuntime {
    bool bound=false,worldReady=false,permitted=false,blockTarget=false,entityTarget=false;
    std::uint64_t session=0,world=0,attackCalls=0,buildProtocol=0,heartbeatAge=0,blockToken=0;
    std::array<std::uint64_t,2> clickRequests{},clickActions{};
    double localTps=0;
    std::string detail="Waiting for exact-build adapter",screen,sha256;
    std::array<ModuleEvidence,EngineModuleCount> modules{};
};
}
