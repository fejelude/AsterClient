#pragma once
#include "config/store.h"
#include <deque>
#include <mutex>
namespace aster {
class Diagnostics {
public:
    explicit Diagnostics(std::filesystem::path directory);
    void event(std::string_view module,std::string_view code,std::string_view detail,
               std::string_view transition="",std::string_view gameBuild="");
    Json recent() const;
    std::string report(const Profile&,const GameSnapshot&,const RuntimeStatus&) const;
    static std::string redact(std::string_view);
private:
    std::filesystem::path directory_;
    mutable std::mutex mutex_;
    std::deque<Json> events_;
    std::uint64_t sequence_=0;
};
std::string utcNow();
} // namespace aster
