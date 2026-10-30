#include "core/planner.h"
#include <algorithm>
#include <cmath>

namespace aster {
InputPlanner::InputPlanner(std::uint32_t seed):random_(seed){}
double InputPlanner::interval(double cps,bool randomize){
    double factor=randomize?std::uniform_real_distribution<double>(.88,1.12)(random_):1.;
    return factor/std::clamp(cps,1.,40.);
}
void InputPlanner::reset(){channels_={};ownedKeys_.clear();permittedLast_=false;waitForRelease_={};}
std::vector<OutputEvent> InputPlanner::stop(const PhysicalInput& input){
    std::vector<OutputEvent> result;
    for(int key:ownedKeys_){
        // Do not release a key physically held by the user.
        if(key>=0&&key<256&&!input.keys[key])result.push_back({OutputKind::KeyUp,key,"cleanup"});
    }
    ownedKeys_.clear();channels_={};permittedLast_=false;
    for(int i=0;i<2;++i)waitForRelease_[i]=input.mouse[i];
    return result;
}
std::vector<OutputEvent> InputPlanner::tick(double now,const Profile& profile,const PhysicalInput& input,const InputGate& gate){
    if(!std::isfinite(now)||!gate.permits())return stop(input);
    std::vector<OutputEvent> events;
    if(!permittedLast_){
        for(int i=0;i<2;++i)waitForRelease_[i]=waitForRelease_[i]||input.mouse[i];
        permittedLast_=true;
    }
    const auto& clicker=profile.modules.at("clicker");
    for(int i=0;i<2;++i){
        auto& channel=channels_[i];
        if(!input.mouse[i]){waitForRelease_[i]=false;channel.held=false;channel.primed=false;}
        bool priming=clicker.flag(i==0?"leftPriming":"rightPriming");
        int other=1-i;
        if(!channel.held&&input.mouse[i]){channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),clicker.flag("randomize"));
            channel.seenPress=input.pressSequence[i];}
        if(input.mouse[i]&&input.pressSequence[other]>channel.seenPress){channel.primed=true;channel.seenPress=input.pressSequence[other];}
        channel.held=input.mouse[i];
        if(!clicker.enabled||waitForRelease_[i]||!channel.held||(priming&&!channel.primed)){
            channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),false);continue;
        }
        if(now+1e-9>=channel.due){
            int output=clicker.flag("invert")?1-i:i;
            events.push_back({OutputKind::Click,output,"clicker"});
            // A delayed frame emits at most one click per stream; no catch-up burst.
            channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),clicker.flag("randomize"));
        }
    }
    std::set<int> wanted;
    if(profile.modules.at("walk").enabled)wanted.insert(profile.movement[0]);
    if(profile.modules.at("sprint").enabled&&(input.keys[profile.movement[0]]||profile.modules.at("walk").enabled))
        wanted.insert(profile.movement[4]);
    for(int key:ownedKeys_)if(!wanted.contains(key)&&!input.keys[key])events.push_back({OutputKind::KeyUp,key,"cleanup"});
    std::set<int> nextOwned;
    for(int key:wanted){
        if(ownedKeys_.contains(key))nextOwned.insert(key);
        else if(!input.keys[key]){events.push_back({OutputKind::KeyDown,key,"movement"});nextOwned.insert(key);}
    }
    ownedKeys_=std::move(nextOwned);
    return events;
}
bool bindingMatches(const Keybind& binding,int key,unsigned modifiers){
    return binding.key!=0&&binding.key==key&&binding.modifiers==(modifiers&7u);
}
} // namespace aster
