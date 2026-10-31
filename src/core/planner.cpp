#include "core/planner.h"
#include <algorithm>
#include <cmath>

namespace aster {
InputPlanner::InputPlanner(std::uint32_t seed):random_(seed){}
double InputPlanner::interval(double cps,bool randomize){
    double factor=randomize?std::uniform_real_distribution<double>(.88,1.12)(random_):1.;
    return factor/std::clamp(cps,1.,40.);
}
void InputPlanner::reset(){channels_={};ownedKeys_.clear();permittedLast_=false;waitForRelease_={};heldMouse_={};blockSince_=-1;tapUntil_=0;lastAttack_=0;forwardSuppressed_=false;}
std::vector<OutputEvent> InputPlanner::stop(const PhysicalInput& input,bool restoreForward){
    std::vector<OutputEvent> result;
    if(restoreForward&&forwardSuppressed_&&suppressedKey_>0&&input.keys[suppressedKey_])result.push_back({OutputKind::KeyDown,suppressedKey_,"physical_restore"});
    for(int key:ownedKeys_){
        // Do not release a key physically held by the user.
        if(key>=0&&key<256&&!input.keys[key])result.push_back({OutputKind::KeyUp,key,"cleanup"});
    }
    ownedKeys_.clear();channels_={};permittedLast_=false;
    for(unsigned i=0;i<2;++i)if(heldMouse_[i])result.push_back({OutputKind::MouseUp,static_cast<int>(i),"cleanup"});
    heldMouse_={};blockSince_=-1;tapUntil_=0;forwardSuppressed_=false;
    for(int i=0;i<2;++i)waitForRelease_[i]=input.mouse[i];
    return result;
}
std::vector<OutputEvent> InputPlanner::tick(double now,const Profile& profile,const PhysicalInput& input,const InputGate& gate,const InputContext& context){
    if(!std::isfinite(now)||!gate.permits()||!context.worldReady){lastAttack_=context.attackSequence;return stop(input,context.restorePhysical);}
    std::vector<OutputEvent> events;
    if(!permittedLast_){
        for(int i=0;i<2;++i)waitForRelease_[i]=waitForRelease_[i]||input.mouse[i];
        permittedLast_=true;
    }
    const auto& clicker=profile.modules.at("clicker");
    const unsigned miningInput=clicker.flag("invert")?1:0;
    if(context.blockTarget&&input.mouse[miningInput]){if(blockSince_<0||blockToken_!=context.blockToken){blockSince_=now;blockToken_=context.blockToken;}}
    else blockSince_=-1;
    std::array<bool,2> wantHeld{};
    for(int i=0;i<2;++i){
        auto& channel=channels_[i];
        if(!input.mouse[i]){waitForRelease_[i]=false;channel.held=false;channel.primed=false;}
        bool priming=clicker.flag(i==0?"leftPriming":"rightPriming");
        int other=1-i;
        if(!channel.held&&input.mouse[i]){channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),clicker.flag("randomize"));
            channel.seenPress=input.pressSequence[i];}
        if(input.mouse[i]&&input.pressSequence[other]>channel.seenPress){channel.primed=true;channel.seenPress=input.pressSequence[other];}
        channel.held=input.mouse[i];
        const bool trigger=i==0&&clicker.flag("triggerbot")&&context.entityTarget;
        if(!clicker.enabled||waitForRelease_[i]||(!channel.held&&!trigger)||(priming&&!channel.primed)){
            channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),false);continue;
        }
        const int output=clicker.flag("invert")?1-i:i;
        const bool mining=output==0&&input.mouse[i]&&context.blockTarget&&(clicker.flag("breakAuto")||(clicker.flag("breakLegit")&&blockSince_>=0&&now-blockSince_>=.25));
        if(mining){wantHeld[output]=true;channel.due=now+interval(clicker.number(i==0?"leftCps":"rightCps"),false);continue;}
        if(now+1e-9>=channel.due){
            events.push_back({OutputKind::Click,output,"clicker"});
            // Preserve the nominal clock through polling jitter, but discard a
            // missed whole interval instead of emitting a catch-up burst.
            const auto period=interval(clicker.number(i==0?"leftCps":"rightCps"),clicker.flag("randomize"));
            const auto next=channel.due+period;channel.due=next>now?next:now+period;
        }
    }
    for(unsigned i=0;i<2;++i){if(wantHeld[i]&&!heldMouse_[i])events.push_back({OutputKind::MouseDown,static_cast<int>(i),"mining"});if(!wantHeld[i]&&heldMouse_[i])events.push_back({OutputKind::MouseUp,static_cast<int>(i),"cleanup"});}heldMouse_=wantHeld;
    bool tapping=now<tapUntil_&&profile.modules.at("stap").enabled;
    if(!tapping&&profile.modules.at("stap").enabled&&context.attackSequence!=lastAttack_&&input.keys[profile.movement[0]]&&!input.keys[profile.movement[2]]){tapUntil_=now+.06;tapping=true;}
    lastAttack_=context.attackSequence;
    if(tapping&&!forwardSuppressed_&&input.keys[profile.movement[0]]){events.push_back({OutputKind::KeyUp,profile.movement[0],"stap"});suppressedKey_=profile.movement[0];forwardSuppressed_=true;}
    if(!tapping&&forwardSuppressed_){if(input.keys[profile.movement[0]])events.push_back({OutputKind::KeyDown,profile.movement[0],"stap_restore"});forwardSuppressed_=false;}
    std::set<int> wanted;
    if(tapping)wanted.insert(profile.movement[2]);
    if(profile.modules.at("walk").enabled&&!tapping)wanted.insert(profile.movement[0]);
    if(!tapping&&profile.modules.at("sprint").enabled&&(input.keys[profile.movement[0]]||profile.modules.at("walk").enabled))
        wanted.insert(profile.movement[4]);
    for(int key:ownedKeys_)if(!wanted.contains(key)&&!input.keys[key])events.push_back({OutputKind::KeyUp,key,"cleanup"});
    std::set<int> nextOwned;
    for(int key:wanted){
        if(ownedKeys_.contains(key))nextOwned.insert(key);
        else if(!input.keys[key]){const char* source=tapping&&key==profile.movement[2]?"stap":profile.modules.at("walk").enabled&&key==profile.movement[0]?"walk":"sprint";events.push_back({OutputKind::KeyDown,key,source});nextOwned.insert(key);}
    }
    ownedKeys_=std::move(nextOwned);
    return events;
}
bool bindingMatches(const Keybind& binding,int key,unsigned modifiers){
    return binding.key!=0&&binding.key==key&&binding.modifiers==(modifiers&7u);
}
} // namespace aster
