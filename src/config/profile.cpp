#include "config/profile.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace aster {
namespace {
template<class T> struct JsonReader {
    static T get(const Json& source){
        if constexpr(std::is_integral_v<T>&&!std::is_same_v<T,bool>){
            // Numeric get<T>() otherwise truncates fractions and wraps overflow.
            if(!source.is_number_integer())throw std::runtime_error("Expected integer");
            const auto number=source.get<long double>();
            if(number<std::numeric_limits<T>::lowest()||number>std::numeric_limits<T>::max())
                throw std::runtime_error("Integer outside representable range");
        }else if constexpr(std::is_floating_point_v<T>){
            if(!source.is_number())throw std::runtime_error("Expected number");
            const auto number=source.get<long double>();
            if(!std::isfinite(number)||std::abs(number)>std::numeric_limits<T>::max())
                throw std::runtime_error("Number outside representable range");
        }
        return source.get<T>();
    }
};
template<class T,std::size_t N> struct JsonReader<std::array<T,N>> {
    static std::array<T,N> get(const Json& source){
        if(!source.is_array()||source.size()!=N)throw std::runtime_error("Invalid array size");
        std::array<T,N> result{};
        for(std::size_t i=0;i<N;++i)result[i]=JsonReader<T>::get(source.at(i));
        return result;
    }
};
template<class T> void read(const Json& j,const char* key,T& value){
    if(j.contains(key))try{value=JsonReader<T>::get(j.at(key));}
    catch(const std::exception&){throw std::runtime_error(std::string("Invalid value: ")+key);}
}
void requireObject(const Json& value,const char* name){
    if(!value.is_object())throw std::runtime_error(std::string(name)+" must be an object");
}
Json valuesJson(const Values& values){
    Json j=Json::object();for(const auto& [k,v]:values)std::visit([&](auto val){j[k]=val;},v);return j;
}
}
Json profileToJson(const Profile& p){
    const auto& u=p.ui;
    Json ui={{"category",u.category},{"tabs",u.tabs},{"scale",u.scale},{"hueSpeed",u.hueSpeed},
        {"rounding",u.rounding},{"animateHue",u.animateHue},{"animateBackground",u.animateBackground},
        {"accent",u.accent},{"border",u.border},{"primary",u.primary},{"secondary",u.secondary},
        {"text",u.text},{"disabled",u.disabled},{"activeText",u.activeText},
        {"huebar",u.huebar},{"huebarHeight",u.huebarHeight},{"footer",u.footer},{"dpiAware",u.dpiAware},
        {"windowLocked",u.windowLocked},{"textureOpacity",u.textureOpacity},{"panelOpacity",u.panelOpacity},
        {"backgroundScale",u.backgroundScale},{"backgroundSpeed",u.backgroundSpeed},
        {"backgroundX",u.backgroundX},{"backgroundY",u.backgroundY},
        {"scaleImage",u.scaleImage},{"accentTint",u.accentTint},{"imagePath",u.imagePath}};
    Json modules=Json::object(),bindings=Json::object();
    for(const auto& [id,m]:p.modules){
        Json profiles=Json::object();for(const auto& [name,v]:m.profiles)profiles[name]=valuesJson(v);
        modules[id]={{"enabled",m.enabled},{"selectedProfile",m.selectedProfile},{"profiles",profiles}};
    }
    for(const auto& [id,key]:p.bindings)bindings[id]={{"key",key.key},{"modifiers",key.modifiers}};
    return {{"schemaVersion",kSchemaVersion},{"ui",ui},{"modules",modules},{"bindings",bindings},
        {"movement",p.movement},{"requireCapturedCursor",p.requireCapturedCursor},
        {"license",{{"endpoint",p.license.endpoint},{"publicKeyPath",p.license.publicKeyPath},{"required",p.license.required}}},
        {"updates",{{"feedUrl",p.updates.feedUrl},{"publicKeyPath",p.updates.publicKeyPath},
                    {"signerThumbprint",p.updates.signerThumbprint},{"channel",p.updates.channel}}}};
}
Profile profileFromJson(Json j,std::vector<std::string>& warnings){
    if(!j.is_object())throw std::runtime_error("Profile must be a JSON object");
    int version=1;read(j,"schemaVersion",version);
    if(version>kSchemaVersion)throw std::runtime_error("FUTURE_SCHEMA");
    if(version<1)throw std::runtime_error("Invalid schema version");
    if(version<kSchemaVersion)warnings.push_back("Migrated schema "+std::to_string(version)+" to 3");
    Profile p=defaultProfile();
    if(j.contains("ui")){
        const auto& ui=j.at("ui");if(!ui.is_object())throw std::runtime_error("ui must be an object");
        auto& u=p.ui;
#define R(field) read(ui,#field,u.field)
        R(category);R(tabs);R(scale);R(hueSpeed);R(rounding);R(animateHue);R(animateBackground);
        R(accent);R(border);R(primary);R(secondary);R(text);R(disabled);R(activeText);
        R(huebar);R(huebarHeight);R(footer);R(dpiAware);R(windowLocked);
        R(textureOpacity);R(panelOpacity);R(backgroundScale);R(backgroundSpeed);R(backgroundX);R(backgroundY);
        R(scaleImage);R(accentTint);R(imagePath);
#undef R
        // Explicit migration of the representative PDF schema.
        if(ui.contains("accentRGB")){
            auto color=JsonReader<std::array<float,3>>::get(ui.at("accentRGB"));
            for(int i=0;i<3;++i)u.accent[i]=color[i]/255.f;
        }
        read(ui,"backgroundOpacity",u.panelOpacity);
        if(ui.contains("activePage")){
            auto page=ui.at("activePage").get<std::string>();
            if(page=="movement")u.category=1;else if(page=="visual")u.category=2;else if(page=="settings")u.category=3;
        }
    }
    if(j.contains("modules")){
        const auto& modules=j.at("modules");if(!modules.is_object())throw std::runtime_error("modules must be an object");
        for(auto& [id,m]:p.modules){
            if(!modules.contains(id))continue;
            const auto& source=modules.at(id);if(!source.is_object())throw std::runtime_error("Invalid module");
            read(source,"enabled",m.enabled);read(source,"selectedProfile",m.selectedProfile);
            if(!m.profiles.contains(m.selectedProfile))m.selectedProfile=(id=="aim"?"closet":"default");
            auto assign=[&](const Json& values,Values& target){
                if(!values.is_object())throw std::runtime_error("Module values must be an object");
                for(auto& [key,value]:target){
                    if(!values.contains(key))continue;
                    if(std::holds_alternative<double>(value)&&values.at(key).is_number())value=values.at(key).get<double>();
                    else if(std::holds_alternative<bool>(value)&&values.at(key).is_boolean())value=values.at(key).get<bool>();
                    else warnings.push_back(id+"."+key+" invalid type; used default");
                }
            };
            if(source.contains("profiles")){
                requireObject(source.at("profiles"),"profiles");
                for(auto& [name,values]:m.profiles){
                    if(source.at("profiles").contains(name))assign(source.at("profiles").at(name),values);
                }
            }else assign(source,m.values());
        }
    }
    if(j.contains("bindings")){
        requireObject(j.at("bindings"),"bindings");
        for(auto& [id,key]:p.bindings)if(j.at("bindings").contains(id)){
            const auto& source=j.at("bindings").at(id);requireObject(source,"binding");read(source,"key",key.key);read(source,"modifiers",key.modifiers);
        }
    }else if(j.contains("keybinds")){
        const auto& keys=j.at("keybinds");
        requireObject(keys,"keybinds");
        for(auto& [id,key]:p.bindings){
            if(!keys.contains(id)||keys.at(id).is_null())continue;
            if(keys.at(id).is_number_integer())key.key=JsonReader<int>::get(keys.at(id));
            else if(keys.at(id).is_string()){
                auto text=keys.at(id).get<std::string>();
                if(text.size()==1)key.key=static_cast<unsigned char>(text[0]);
                else if(text=="TAB")key.key=0x09;
                else if(text=="SHIFT")key.key=0x10;
                else warnings.push_back("Unrecognized legacy binding: "+id);
            }
        }
    }
    read(j,"movement",p.movement);read(j,"requireCapturedCursor",p.requireCapturedCursor);
    if(j.contains("movementKeys")){
        requireObject(j.at("movementKeys"),"movementKeys");
        const char* names[]={"forward","left","backward","right","sprint","crouch"};
        for(int i=0;i<6;++i)if(j.at("movementKeys").contains(names[i])){
            auto value=j.at("movementKeys").at(names[i]).get<std::string>();
            if(value.size()==1)p.movement[i]=static_cast<unsigned char>(value[0]);
            else if(value=="SHIFT")p.movement[i]=0x10;
            else if(value=="CTRL")p.movement[i]=0x11;
        }
    }
    if(j.contains("license")){const auto& l=j.at("license");requireObject(l,"license");read(l,"endpoint",p.license.endpoint);
        read(l,"publicKeyPath",p.license.publicKeyPath);read(l,"required",p.license.required);}
    if(j.contains("updates")){const auto& u=j.at("updates");requireObject(u,"updates");read(u,"feedUrl",p.updates.feedUrl);
        read(u,"publicKeyPath",p.updates.publicKeyPath);read(u,"signerThumbprint",p.updates.signerThumbprint);read(u,"channel",p.updates.channel);}
    auto fixes=validate(p);warnings.insert(warnings.end(),fixes.begin(),fixes.end());return p;
}
} // namespace aster
