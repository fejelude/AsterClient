#include "config/store.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace aster {
namespace {
template<class T> void read(const Json& j,const char* key,T& value){
    if(j.contains(key)) { try{value=j.at(key).get<T>();}catch(const Json::exception&){throw std::runtime_error(std::string("Invalid type: ")+key);} }
}
Json valuesJson(const Values& values){
    Json j=Json::object();for(const auto& [k,v]:values)std::visit([&](auto val){j[k]=val;},v);return j;
}
std::string stamp(){return std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId());}
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
            auto color=ui.at("accentRGB").get<std::array<float,3>>();
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
                for(auto& [name,values]:m.profiles){
                    if(source.at("profiles").contains(name))assign(source.at("profiles").at(name),values);
                }
            }else assign(source,m.values());
        }
    }
    if(j.contains("bindings")){
        for(auto& [id,key]:p.bindings)if(j.at("bindings").contains(id)){
            auto& source=j.at("bindings").at(id);read(source,"key",key.key);read(source,"modifiers",key.modifiers);
        }
    }else if(j.contains("keybinds")){
        const auto& keys=j.at("keybinds");
        for(auto& [id,key]:p.bindings){
            if(!keys.contains(id)||keys.at(id).is_null())continue;
            if(keys.at(id).is_number_integer())key.key=keys.at(id).get<int>();
            else if(keys.at(id).is_string()){
                auto text=keys.at(id).get<std::string>();
                if(text.size()==1)key.key=static_cast<unsigned char>(text[0]);
                else if(text=="TAB")key.key=VK_TAB;
                else if(text=="SHIFT")key.key=VK_SHIFT;
                else warnings.push_back("Unrecognized legacy binding: "+id);
            }
        }
    }
    read(j,"movement",p.movement);read(j,"requireCapturedCursor",p.requireCapturedCursor);
    if(j.contains("movementKeys")){
        const char* names[]={"forward","left","backward","right","sprint","crouch"};
        for(int i=0;i<6;++i)if(j.at("movementKeys").contains(names[i])){
            auto value=j.at("movementKeys").at(names[i]).get<std::string>();
            if(value.size()==1)p.movement[i]=static_cast<unsigned char>(value[0]);
            else if(value=="SHIFT")p.movement[i]=VK_SHIFT;
            else if(value=="CTRL")p.movement[i]=VK_CONTROL;
        }
    }
    if(j.contains("license")){const auto& l=j.at("license");read(l,"endpoint",p.license.endpoint);
        read(l,"publicKeyPath",p.license.publicKeyPath);read(l,"required",p.license.required);}
    if(j.contains("updates")){const auto& u=j.at("updates");read(u,"feedUrl",p.updates.feedUrl);
        read(u,"publicKeyPath",p.updates.publicKeyPath);read(u,"signerThumbprint",p.updates.signerThumbprint);read(u,"channel",p.updates.channel);}
    auto fixes=validate(p);warnings.insert(warnings.end(),fixes.begin(),fixes.end());return p;
}
std::string readFile(const std::filesystem::path& path,std::size_t maxBytes){
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("Cannot open file");
    auto size=stream.tellg();if(size<0||static_cast<std::uint64_t>(size)>maxBytes)throw std::runtime_error("File exceeds size limit");
    std::string bytes(static_cast<std::size_t>(size),'\0');stream.seekg(0);
    if(!bytes.empty()&&!stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read file");
    return bytes;
}
void atomicWrite(const std::filesystem::path& path,std::string_view bytes,bool backup){
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    auto temp=path;temp+=L".tmp-"+std::filesystem::path(stamp()).wstring();
    HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::system_error(GetLastError(),std::system_category(),"Create profile temp file");
    DWORD count=0;bool ok=bytes.size()<=MAXDWORD&&WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file);
    DWORD error=ok?ERROR_SUCCESS:GetLastError();CloseHandle(file);
    if(!ok){DeleteFileW(temp.c_str());throw std::system_error(error,std::system_category(),"Flush profile");}
    bool exists=std::filesystem::exists(path);
    if(exists){
        auto bak=path;bak+=L".bak";
        if(!ReplaceFileW(path.c_str(),temp.c_str(),backup?bak.c_str():nullptr,0,nullptr,nullptr)){
            error=GetLastError();DeleteFileW(temp.c_str());throw std::system_error(error,std::system_category(),"Replace profile");
        }
    }else if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)){
        error=GetLastError();DeleteFileW(temp.c_str());throw std::system_error(error,std::system_category(),"Commit profile");
    }
}
ConfigStore::ConfigStore(std::filesystem::path path):path_(std::move(path)){}
LoadResult ConfigStore::load(){
    std::lock_guard lock(mutex_);LoadResult result;result.profile=defaultProfile();readOnly_=false;preserveBackupOnSave_=false;
    if(!std::filesystem::exists(path_))return result;
    try{result.profile=profileFromJson(Json::parse(readFile(path_)),result.warnings);return result;}
    catch(const std::exception& error){
        if(std::string(error.what())=="FUTURE_SCHEMA"){
            readOnly_=true;result.readOnly=true;result.warnings.emplace_back("Profile was written by a newer app. It is read-only; original file preserved.");return result;
        }
        auto corrupt=path_;corrupt+=L".corrupt-"+std::filesystem::path(stamp()).wstring();
        std::error_code ec;std::filesystem::copy_file(path_,corrupt,std::filesystem::copy_options::none,ec);
        if(ec){readOnly_=true;result.readOnly=true;result.warnings.emplace_back("Cannot preserve invalid profile. Saving disabled to protect it.");return result;}
        result.recovered=true;preserveBackupOnSave_=true;result.warnings.emplace_back("Invalid profile preserved as .corrupt; recovering backup or defaults.");
        auto backup=path_;backup+=L".bak";
        if(std::filesystem::exists(backup))try{result.profile=profileFromJson(Json::parse(readFile(backup)),result.warnings);result.warnings.emplace_back("Loaded last valid backup.");}
        catch(...){result.warnings.emplace_back("Backup also invalid; loaded defaults.");}
        return result;
    }
}
void ConfigStore::save(const Profile& profile){
    std::lock_guard lock(mutex_);if(readOnly_)throw std::runtime_error("Profile is read-only to preserve a newer or unrecoverable file");
    Profile checked=profile;validate(checked);
    auto serialized=profileToJson(checked).dump(2)+"\n";
    std::vector<std::string> warnings;
    (void)profileFromJson(Json::parse(serialized),warnings);
    atomicWrite(path_,serialized,!preserveBackupOnSave_);preserveBackupOnSave_=false;
}
std::filesystem::path defaultConfigPath(){
    PWSTR path=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&path)))throw std::runtime_error("Cannot locate AppData");
    auto result=std::filesystem::path(path)/L"AsterClient"/L"profiles"/L"default.json";CoTaskMemFree(path);return result;
}
} // namespace aster
