#pragma once
#include "core/model.h"
#include <filesystem>
#include <mutex>
#include "json.hpp"
namespace aster {
using Json = nlohmann::json;
Json profileToJson(const Profile& profile);
Profile profileFromJson(Json data, std::vector<std::string>& warnings);
struct LoadResult {
    Profile profile;
    std::vector<std::string> warnings;
    bool recovered = false, readOnly = false;
};
class ConfigStore {
public:
    explicit ConfigStore(std::filesystem::path path);
    LoadResult load();
    void save(const Profile& profile);
    const std::filesystem::path& path() const { return path_; }
    bool readOnly() const { return readOnly_; }
private:
    std::filesystem::path path_;
    bool readOnly_ = false;
    bool preserveBackupOnSave_ = false;
    std::mutex mutex_;
};
std::filesystem::path defaultConfigPath();
void atomicWrite(const std::filesystem::path& destination, std::string_view bytes, bool backup = true);
std::string readFile(const std::filesystem::path& path, std::size_t maxBytes = 4*1024*1024);
} // namespace aster
