#include "diagnostics/log.h"
#include <Windows.h>
#include <fstream>
#include <regex>
#include <sstream>
#include <iomanip>
namespace aster {
std::string utcNow(){
    SYSTEMTIME t{};GetSystemTime(&t);char out[40];
    snprintf(out,sizeof(out),"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
    return out;
}
Diagnostics::Diagnostics(std::filesystem::path directory):directory_(std::move(directory)){
    std::filesystem::create_directories(directory_);
}
std::string Diagnostics::redact(std::string_view value){
    static const std::regex paths(R"([A-Za-z]:[\\/][^\r\n\"<>]*)");
    static const std::regex secrets(R"((accessToken|refreshToken|licenseKey|purchaseKey|authorization|password)\s*[:=]\s*[^\s,;]+)",std::regex::icase);
    auto text=std::regex_replace(std::string(value),secrets,"[secret redacted]");
    text=std::regex_replace(text,paths,"[local path]");
    if(text.size()>700)text.resize(700);return text;
}
void Diagnostics::event(std::string_view module,std::string_view code,std::string_view detail,std::string_view transition,std::string_view build){
    std::lock_guard lock(mutex_);
    Json e={{"timeUtc",utcNow()},{"appVersion",kVersion},{"gameVersion",build},
        {"module",module},{"transition",transition},{"code",code},{"detail",redact(detail)},
        {"correlationId",++sequence_}};
    events_.push_back(e);if(events_.size()>200)events_.pop_front();
    try{
        auto path=directory_/L"events.jsonl";
        if(std::filesystem::exists(path)&&std::filesystem::file_size(path)>2*1024*1024){
            auto previous=directory_/L"events.previous.jsonl";
            std::error_code ec;std::filesystem::remove(previous,ec);std::filesystem::rename(path,previous,ec);
        }
        std::ofstream stream(path,std::ios::binary|std::ios::app);stream<<e.dump()<<'\n';
    }catch(...){/* Disk failure must not stop the UI or input cancellation. */}
}
Json Diagnostics::recent()const{std::lock_guard lock(mutex_);return Json(events_);}
std::string Diagnostics::report(const Profile& p,const GameSnapshot& game,const RuntimeStatus& runtime)const{
    Json modules=Json::object();
    for(const auto& d:moduleDefinitions()){
        auto health=computeHealth(d,p.modules.at(d.id),game,runtime);
        modules[d.id]={{"requested",health.enabledByUser},{"state",stateName(health.state)},
            {"code",health.code},{"detail",health.detail}};
    }
    Json report={{"app","Aster Client"},{"version",kVersion},{"createdUtc",utcNow()},
        {"game",{{"present",game.present},{"identityVerified",game.identityVerified},{"x64",game.x64},
                 {"build",game.build},{"foreground",game.foreground},{"cursorCaptured",game.cursorCaptured}}},
        {"input",{{"armed",runtime.armed},{"requestedClicks",runtime.requested},{"windowsAcceptedClicks",runtime.sent},
                  {"gameAcknowledgments",nullptr},{"failures",runtime.failures}}},
        {"modules",modules},{"events",recent()},
        {"privacy","No keys, tokens, file paths, input history, account names or machine identifiers included."}};
    return report.dump(2);
}
} // namespace aster
