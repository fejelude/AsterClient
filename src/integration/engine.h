#pragma once
#include "core/planner.h"
#include "integration/shared.h"
#include <filesystem>
#include <memory>
namespace aster {
class GameDetector;
class Diagnostics;
inline constexpr char kSupportedGameSha256[]="988f6a61403b4381aa41e6c66d64cce441b14e561a9b10f647c2758ef3f2672a";
class BridgeSession {
public:
    BridgeSession();
    ~BridgeSession();
    BridgeSession(const BridgeSession&)=delete;
    BridgeSession& operator=(const BridgeSession&)=delete;
    void connect(const GameSnapshot&,const std::filesystem::path& dll);
    void send(bridge::Control);
    bool read(bridge::Telemetry&)const;
    bool alive()const;
    void close();
    std::uint64_t id()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
bridge::Control bridgeConfiguration(const Profile&);
class EngineService {
public:
    EngineService(GameDetector&,Diagnostics&);
    ~EngineService();
    void configure(const Profile&);
    void input(const PhysicalInput&,bool armed,bool licensed,const std::array<std::uint64_t,2>& clicks,const std::array<bool,2>& mining,std::uint64_t world);
    EngineRuntime status()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
