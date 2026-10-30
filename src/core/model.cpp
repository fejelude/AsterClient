#include "core/model.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aster {
namespace {
SettingDefinition n(std::string id, std::string label, double initial, double low, double high,
                    double step, std::string format, std::string help, bool implemented=true) {
    return {std::move(id),std::move(label),SettingKind::Number,initial,low,high,step,std::move(format),std::move(help),implemented};
}
SettingDefinition b(std::string id, std::string label, bool initial, std::string help, bool implemented=true) {
    return {std::move(id),std::move(label),SettingKind::Boolean,initial?1.:0.,0.,1.,1.,"",std::move(help),implemented};
}
Values defaults(const ModuleDefinition& def) {
    Values values;
    for(const auto& s:def.settings) values[s.id] = s.kind==SettingKind::Boolean?Value{s.initial!=0}:Value{s.initial};
    return values;
}
}
const std::vector<ModuleDefinition>& moduleDefinitions() {
    static const std::vector<ModuleDefinition> defs{
        {"reach","Reach","Combat",Capability::Engine,"Interaction distance; requires a tested game-engine adapter.",{
            n("distance","Reach",5.2,3.,10.,.01,"%.2f m","Independent configuration bounds: 3-10 m. Nitro's actual bounds are unverified."),
            b("combatMode","Combat Mode",true,"Only activate reach while clicking."),
            b("onlyMoving","Only Moving",false,"Require player motion from verified game telemetry.")}},
        {"clicker","Clicker","Combat",Capability::ClickInput,"Independent hold-to-repeat mouse streams. Game acceptance is unverified.",{
            n("leftCps","Left",13,1,40,1,"%.0f CPS","Hold the physical left button to repeat. Range: 1-40 clicks per second."),
            n("rightCps","Right",30,1,40,1,"%.0f CPS","The right stream has its own clock; holding both keeps them independent."),
            n("tuner","Tuner",651,0,1000,1,"%.0f","Unsupported: the reference does not define this algorithm or its units.",false),
            b("leftPriming","Left Priming",false,"Hold left and tap right to prime the left stream. Independent mirrored policy; not verified Nitro parity."),
            b("rightPriming","Right Priming",false,"Hold right-click and tap left-click to activate right-clicker."),
            b("randomize","Randomize",false,"Uniform interval jitter of +/-12%; expected interval remains 1/CPS."),
            b("triggerbot","Triggerbot",false,"Unsupported: no verified crosshair target telemetry.",false),
            b("breakAuto","Break Blocks (Auto)",false,"Unsupported: requires verified block targeting and reference semantics.",false),
            b("breakLegit","Break Blocks (Legit)",false,"Unsupported: the Auto/Legit distinction has not been established.",false),
            b("invert","Invert Clicks",false,"Swap logical output buttons while preserving independent physical input streams.")}},
        {"aim","Aim Assist","Combat",Capability::Engine,"Separate Closet and Blatant profiles; no target data is obtained from the game.",{
            n("smoothness","Smoothness",25,0,100,1,"%.0f%%","Saved profile value. The reference smoothing equation is unknown."),
            n("fovHorizontal","FOV / first value",.4,0,180,.01,"%.2f","The screenshot units are unverified; this is configuration only."),
            n("fovVertical","FOV / second value",.02,0,180,.01,"%.2f","The second FOV value has no established runtime interpretation."),
            n("distance","Distance",5,1,100,.1,"%.2f m","Maximum candidate target distance."),
            n("height","Height",-.3,-2,2,.01,"%.2f","Vertical target offset; units require game validation."),
            b("vertical","Vertical",false,"Allow pitch adjustment when a compatible adapter exists."),
            b("horizontal","Horizontal",true,"Allow yaw adjustment when a compatible adapter exists."),
            b("antibot","Antibot",false,"Classifier not implemented; no performance claim is made."),
            b("onlyMoving","Only Moving",false,"Require player motion."),
            b("avoidWalls","Avoid Walls",true,"Require verified line of sight."),
            b("avoidOnTarget","Avoid On Target",false,"Unverified reference policy; saved as configuration."),
            b("cacheTargets","Cache Targets",true,"Cache invalidation requires world/entity identity."),
            b("combatMode","Combat Mode",false,"Only activate while clicking.")}},
        {"hitbox","Hitbox","Combat",Capability::Engine,"Hitbox width and deterministic filtering require verified entity layouts.",{
            n("width","Width",2.22,.1,5,.01,"%.2f","Width units and supported entities remain unverified."),
            b("deterministic","Deterministic",true,"Intended to use Aim Assist target settings; dependency is unverified.")}},
        {"velocity","Velocity","Movement",Capability::Engine,"Independent horizontal and vertical configuration. Curve is a preview, not sampled game motion.",{
            n("horizontal","Linear / X-Z",92.55,0,100,.01,"%.2f%%","Horizontal knockback multiplier; 100% is unchanged."),
            n("vertical","Height / Y",92.55,0,100,.01,"%.2f%%","Vertical knockback multiplier; 100% is unchanged."),
            b("percentage","Percentage",true,"Show normalized axis factors in the preview."),
            b("precision","Precision",true,"Use hundredth-percent slider steps; otherwise whole-percent steps."),
            b("combatMode","Combat Mode",true,"Only activate velocity while clicking."),
            b("combineModifiers","Combine Modifiers",false,"Reference modifier composition is undocumented."),
            b("noBackwards","No Backwards",false,"Reference backward-motion policy is undocumented.")}},
        {"timer","Timer","Movement",Capability::Engine,"Accurate requested-TPS calculations; no changes to Minecraft ticking.",{
            n("multiplier","Timer",1.06,1,5,.01,"%.2fx","Requested TPS = 20 x multiplier. This readout does not measure game TPS."),
            b("combatMode","Combat Mode",true,"Only activate timer while clicking.")}},
        {"zoom","Zoom","Visual",Capability::Engine,"Field-of-view transition requires a tested camera adapter.",{
            n("speedPercent","Zoom Speed",0,0,100,1,"%.0f%%","Controls transition speed; target factor and activation remain unverified."),
            b("lowCpuMode","Low CPU Mode",true,"Reference optimization is unverified; no runtime claim.")}},
        {"auto360","Auto 360","Visual",Capability::Engine,"Rotation units are undocumented. No mouse motion is emitted by this module.",{
            n("speed","Speed",2,.1,10,.01,"%.2f","Controls the speed of Auto 360; units require measurement."),
            n("leftPercent","Left",50,0,100,1,"%.0f%%","Independent reference parameter; normalization is not assumed."),
            n("rightPercent","Right",50,0,100,1,"%.0f%%","Independent reference parameter; normalization is not assumed."),
            n("step","Step",.0001,.000001,.1,.000001,"%.6f","Reference angular step and timing relation are unknown."),
            n("interval","Interval",0,0,10000,1,"%.0f","Allows spins to alternate over intervals; units are unknown.")}},
        {"sprint","Sprint","Movement",Capability::SprintInput,"Hold the configured sprint key while moving forward; matches your Minecraft key map.",{}},
        {"stap","Auto S-Tap","Movement",Capability::Engine,"Unsupported: only a name/hotkey is documented; no trigger schedule is established.",{}},
        {"walk","Auto Walk","Movement",Capability::WalkInput,"Sustain the configured forward key only while the input session is armed and Minecraft is focused.",{}}
    };
    return defs;
}
const ModuleDefinition& definition(std::string_view id) {
    for(const auto& d:moduleDefinitions()) if(d.id==id) return d;
    throw std::invalid_argument("Unknown module");
}
Values& ModuleConfig::values(){ return profiles.at(selectedProfile); }
const Values& ModuleConfig::values() const { return profiles.at(selectedProfile); }
double ModuleConfig::number(std::string_view key) const { return std::get<double>(values().at(std::string(key))); }
bool ModuleConfig::flag(std::string_view key) const { return std::get<bool>(values().at(std::string(key))); }
void ModuleConfig::number(std::string_view key,double value){ values().at(std::string(key))=value; }
void ModuleConfig::flag(std::string_view key,bool value){ values().at(std::string(key))=value; }
Profile defaultProfile(){
    Profile p;
    p.bindings["hide"]={0x79,0}; // F10; tray icon is an independent restore path.
    for(const auto& d:moduleDefinitions()){
        ModuleConfig m;
        m.profiles["default"]=defaults(d);
        if(d.id=="aim"){
            m.profiles.clear(); m.profiles["closet"]=defaults(d); m.profiles["blatant"]=defaults(d);
            m.profiles["blatant"]["smoothness"]=5.; m.profiles["blatant"]["distance"]=100.;
            m.profiles["blatant"]["fovHorizontal"]=4.; m.profiles["blatant"]["fovVertical"]=0.;
            m.selectedProfile="closet";
        }
        p.modules.emplace(d.id,std::move(m)); p.bindings[d.id]={};
    }
    return p;
}
bool isModifierKey(int key){return key==0x10||key==0x11||key==0x12||(key>=0xA0&&key<=0xA5)||key==0x5B||key==0x5C;}
bool isSafeBinding(const Keybind& key,bool global){
    if(key.key==0) return true;
    if(key.key<8||key.key>254||isModifierKey(key.key)||key.key==0x77||key.key==0x78) return false; // F8/F9 reserved
    if(key.modifiers&~7u) return false; // Alt, Ctrl, Shift only
    if(global && key.key<0x70 && !(key.modifiers&3u)) return false;
    return !(key.key==0x73&&(key.modifiers&1u)); // Alt+F4
}
std::vector<std::string> bindingConflicts(const Profile& p){
    std::vector<std::string> result;
    for(auto a=p.bindings.begin();a!=p.bindings.end();++a){
        if(!a->second.key) continue;
        for(auto b2=std::next(a);b2!=p.bindings.end();++b2)
            if(a->second==b2->second) result.push_back(a->first+" conflicts with "+b2->first);
    }
    return result;
}
std::vector<std::string> validate(Profile& p){
    auto defaultsP=defaultProfile(); std::vector<std::string> warnings;
    auto clamp=[&](auto& value,auto low,auto high,const char* name){
        auto old=value; if(!std::isfinite(static_cast<double>(value))) value=low;
        value=std::clamp(value,low,high); if(value!=old) warnings.emplace_back(std::string(name)+" was clamped");
    };
    clamp(p.ui.category,0,3,"category");
    for(int i=0;i<4;++i) clamp(p.ui.tabs[i],0,std::array{3,1,4,2}[i],"tab");
    clamp(p.ui.scale,.8f,1.6f,"UI scale"); clamp(p.ui.hueSpeed,0.f,10.f,"hue speed");
    clamp(p.ui.rounding,0.f,24.f,"rounding");
    clamp(p.ui.textureOpacity,0.f,1.f,"texture opacity"); clamp(p.ui.panelOpacity,.1f,1.f,"panel opacity");
    clamp(p.ui.backgroundScale,.1f,5.f,"background scale"); clamp(p.ui.backgroundSpeed,0.f,4.f,"background speed");
    clamp(p.ui.backgroundX,-2000.f,2000.f,"background x"); clamp(p.ui.backgroundY,-2000.f,2000.f,"background y");
    clamp(p.ui.huebar,0.f,1.f,"huebar"); clamp(p.ui.huebarHeight,1.f,8.f,"huebar height");
    for(Color* color:{&p.ui.accent,&p.ui.border,&p.ui.primary,&p.ui.secondary,&p.ui.text,&p.ui.disabled,&p.ui.activeText})
        for(auto& component:*color) clamp(component,0.f,1.f,"color");
    for(const auto& d:moduleDefinitions()){
        auto [it,inserted]=p.modules.try_emplace(d.id,defaultsP.modules.at(d.id));
        auto& m=it->second; const auto& defM=defaultsP.modules.at(d.id);
        for(const auto& [profileId,defValues]:defM.profiles){
            auto& values=m.profiles[profileId];
            for(const auto& s:d.settings){
                auto value=values.find(s.id);
                if(value==values.end()||value->second.index()!=defValues.at(s.id).index()){
                    values[s.id]=defValues.at(s.id); if(!inserted) warnings.push_back(d.id+"."+s.id+" reset to default");
                }else if(s.kind==SettingKind::Number){
                    double number=std::get<double>(value->second);
                    if(!std::isfinite(number)) number=s.initial;
                    double fixed=std::clamp(number,s.minimum,s.maximum);
                    if(fixed!=number) warnings.push_back(d.id+"."+s.id+" clamped");
                    value->second=fixed;
                }
            }
            for(auto v=values.begin();v!=values.end();) if(!defValues.contains(v->first)) v=values.erase(v); else ++v;
        }
        for(auto v=m.profiles.begin();v!=m.profiles.end();) if(!defM.profiles.contains(v->first))v=m.profiles.erase(v);else ++v;
        if(!m.profiles.contains(m.selectedProfile))m.selectedProfile=defM.selectedProfile;
    }
    for(auto i=p.modules.begin();i!=p.modules.end();)if(!defaultsP.modules.contains(i->first))i=p.modules.erase(i);else ++i;
    for(const auto& [id,key]:defaultsP.bindings){
        auto [it,inserted]=p.bindings.try_emplace(id,key);
        if(!isSafeBinding(it->second,id=="hide")){it->second={};warnings.push_back(id+" unsafe hotkey was unbound");}
    }
    for(auto i=p.bindings.begin();i!=p.bindings.end();)if(!defaultsP.bindings.contains(i->first))i=p.bindings.erase(i);else ++i;
    for(std::size_t i=0;i<p.movement.size();++i){
        if(p.movement[i]<8||p.movement[i]>254||p.movement[i]==0x77||p.movement[i]==0x78){
            p.movement[i]=defaultsP.movement[i]; warnings.emplace_back("Invalid movement key reset");
        }
    }
    if(p.updates.channel!="stable"&&p.updates.channel!="preview")p.updates.channel="stable";
    p.schemaVersion=kSchemaVersion; return warnings;
}
ModuleHealth computeHealth(const ModuleDefinition& d,const ModuleConfig& m,const GameSnapshot& g,const RuntimeStatus& r){
    ModuleHealth h;h.enabledByUser=m.enabled;
    h.support=d.capability==Capability::Engine?SupportState::Unsupported:SupportState::Partial;
    if(!m.enabled){h.state=ModuleState::Disabled;h.detail="Disabled by you";return h;}
    if(d.capability==Capability::Engine){h.state=ModuleState::Unsupported;h.code="NO_VERIFIED_ENGINE_ADAPTER";
        h.detail="No verified engine adapter"+(g.build.empty()?std::string{}:" for "+g.build)+". Configuration is saved; no game changes are made.";return h;}
    if(!g.present){h.state=ModuleState::EnabledOffline;h.code="GAME_NOT_RUNNING";h.detail="Waiting for Minecraft";return h;}
    if(!g.identityVerified||!g.x64){h.state=ModuleState::Unsupported;h.code="GAME_IDENTITY_UNVERIFIED";h.detail=g.detail;return h;}
    if(g.build!=kInputResearchBuild){h.state=ModuleState::Unsupported;h.code="UNSUPPORTED_BUILD";h.detail="Input is disabled for this untested build. Configuration is preserved.";return h;}
    if(!r.licensed){h.state=ModuleState::EnabledOffline;h.code="LICENSE_UNAVAILABLE";h.detail="Your configured license service has not granted an entitlement.";return h;}
    if(!r.inputHealthy){h.state=ModuleState::Failed;h.code="INPUT_SEND_FAILED";h.detail="Windows rejected input. Press F9, review diagnostics, then re-arm.";return h;}
    if(!g.foreground||!r.armed){h.state=ModuleState::EnabledOffline;h.code="INPUT_PAUSED";h.detail=r.stopReason;return h;}
    h.state=ModuleState::InputRunningUnverified;h.code="GAME_EFFECT_UNVERIFIED";
    h.detail="Input scheduler armed. Windows send counts are separate from Minecraft acknowledgments (not available).";return h;
}
std::string_view stateName(ModuleState s){
    switch(s){case ModuleState::Disabled:return "OFF";case ModuleState::EnabledOffline:return "WAITING";
    case ModuleState::Initializing:return "INITIALIZING";case ModuleState::InputRunningUnverified:return "INPUT / UNVERIFIED";
    case ModuleState::Active:return "ACTIVE";case ModuleState::Unsupported:return "UNSUPPORTED";case ModuleState::Failed:return "FAILED";}
    return "UNKNOWN";
}
double timerTps(double x){return 20.*std::clamp(x,1.,5.);}
double timerBoost(double x){return (std::clamp(x,1.,5.)-1.)*100.;}
double wrappedDegrees(double d){return std::remainder(d,360.);}
Orientation lookAt(Vec3 s,Vec3 t){
    const double x=t.x-s.x,y=t.y-s.y,z=t.z-s.z;
    const double factor=180./std::numbers::pi;
    return {wrappedDegrees(std::atan2(-x,z)*factor),std::clamp(-std::atan2(y,std::hypot(x,z))*factor,-90.,90.)};
}
Orientation smoothOrientation(Orientation from,Orientation to,double rate,double dt){
    const double limit=std::max(0.,rate)*std::clamp(dt,0.,.25);
    return {wrappedDegrees(from.yaw+std::clamp(wrappedDegrees(to.yaw-from.yaw),-limit,limit)),
        std::clamp(from.pitch+std::clamp(to.pitch-from.pitch,-limit,limit),-90.,90.)};
}
std::vector<std::uint64_t> eligibleTargets(Vec3 source,Orientation view,const std::vector<Target>& targets,const TargetPolicy& p){
    std::vector<std::pair<double,std::uint64_t>> candidates;
    for(const auto& t:targets){
        if(t.self||t.friendTarget||(p.avoidWalls&&!t.visible)||(p.filterBots&&t.bot))continue;
        double distance=std::hypot(std::hypot(t.position.x-source.x,t.position.y-source.y),t.position.z-source.z);
        if(distance<=0.000001||distance>p.maxDistance)continue;
        auto angle=lookAt(source,t.position);
        const double yaw=std::abs(wrappedDegrees(angle.yaw-view.yaw)),pitch=std::abs(angle.pitch-view.pitch);
        if(yaw>p.maxYaw||pitch>p.maxPitch)continue;
        candidates.emplace_back(yaw*yaw+pitch*pitch,t.id);
    }
    std::sort(candidates.begin(),candidates.end());
    std::vector<std::uint64_t> ids;for(auto [score,id]:candidates){(void)score;ids.push_back(id);}return ids;
}
} // namespace aster
