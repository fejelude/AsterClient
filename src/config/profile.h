#pragma once
#include "core/model.h"
#include "json.hpp"
namespace aster {
using Json = nlohmann::json;
Json profileToJson(const Profile& profile);
Profile profileFromJson(Json data, std::vector<std::string>& warnings);
} // namespace aster
