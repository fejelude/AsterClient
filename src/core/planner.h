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
};
struct InputGate {
    bool armed=false, gameVerified=false, foreground=false, cursorReady=false, licensed=true;
    bool permits() const {return armed&&gameVerified&&foreground&&cursorReady&&licensed;}
};
enum class OutputKind { Click, KeyDown, KeyUp };
struct OutputEvent { OutputKind kind; int code; std::string module; };
class InputPlanner {
public:
    explicit InputPlanner(std::uint32_t seed=0xA57E);
    std::vector<OutputEvent> tick(double time,const Profile&,const PhysicalInput&,const InputGate&);
    std::vector<OutputEvent> stop(const PhysicalInput&);
    void reset();
private:
    struct Channel { bool held=false, primed=false; double due=0; std::uint64_t seenPress=0; };
    std::array<Channel,2> channels_{};
    std::set<int> ownedKeys_;
    bool permittedLast_=false;
    std::array<bool,2> waitForRelease_{};
    std::mt19937 random_;
    double interval(double cps,bool randomize);
};
bool bindingMatches(const Keybind&,int key,unsigned modifiers);
} // namespace aster
