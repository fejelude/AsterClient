#include "config/store.h"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace aster {
namespace {
std::string stamp(){
    static std::atomic<std::uint64_t> sequence=0;
    return std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++sequence);
}
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
