#pragma once
#include "core/model.h"
#include <random>
#include <set>
namespace aster {
struct PhysicalInput {
    std::array<bool,256> keys{};
    std::array<bool,2> mouse{};
    std::array<std::uint64_t,2> pressSequence{};
    std::uint64_t eventSequence=0;
    std::uint64_t pauseSequence=0;
    int pauseKey=0;
};
struct InputGate {
    bool armed=false, gameVerified=false, foreground=false, cursorReady=false, licensed=true;
    bool permits() const {return armed&&gameVerified&&foreground&&cursorReady&&licensed;}
};
enum class OutputKind { Click, KeyDown, KeyUp, MouseDown, MouseUp };
struct InputContext {bool worldReady=true,blockTarget=false,entityTarget=false;std::uint64_t blockToken=0,attackSequence=0;bool restorePhysical=false;};
struct OutputEvent { OutputKind kind; int code; std::string module; };
class InputPlanner {
public:
    explicit InputPlanner(std::uint32_t seed=0xA57E);
    std::vector<OutputEvent> tick(double time,const Profile&,const PhysicalInput&,const InputGate&,const InputContext& context={});
    std::vector<OutputEvent> stop(const PhysicalInput&,bool restoreForward=false);
    void reset();
    bool ownsKey(int key)const{return ownedKeys_.contains(key);}
private:
    struct Channel { bool held=false, primed=false; double due=0; std::uint64_t seenPress=0; };
    std::array<Channel,2> channels_{};
    std::set<int> ownedKeys_;
    bool permittedLast_=false;
    std::array<bool,2> waitForRelease_{};
    std::array<bool,2> heldMouse_{};
    double blockSince_=-1,tapUntil_=0;
    std::uint64_t blockToken_=0,lastAttack_=0;
    bool forwardSuppressed_=false;
    int suppressedKey_=0;
    std::mt19937 random_;
    double interval(double cps,bool randomize);
};
bool bindingMatches(const Keybind&,int key,unsigned modifiers);
} // namespace aster
