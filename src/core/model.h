#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aster {
inline constexpr char kVersion[] = "0.1.0-dev";
inline constexpr int kSchemaVersion = 3;
inline constexpr std::string_view kInputResearchBuild = "1.26.6005.0";
enum class ConnectionState { LookingForGame, Detected, CheckingCompatibility, Connected, Failed };
enum class ModuleState { Disabled, EnabledOffline, Initializing, InputRunningUnverified, Active, Unsupported, Failed };
enum class SupportState { Unknown, Supported, Partial, Unsupported };
enum class SettingKind { Number, Boolean };
enum class Capability { Engine, ClickInput, WalkInput, SprintInput };
using Value = std::variant<double, bool>;
using Values = std::map<std::string, Value, std::less<>>;

struct SettingDefinition {
    std::string id, label;
    SettingKind kind = SettingKind::Number;
    double initial = 0, minimum = 0, maximum = 1, step = .01;
    std::string format = "%.2f", help;
    bool implemented = true;
};
struct ModuleDefinition {
    std::string id, name, category;
    Capability capability = Capability::Engine;
    std::string description;
    std::vector<SettingDefinition> settings;
};
const std::vector<ModuleDefinition>& moduleDefinitions();
const ModuleDefinition& definition(std::string_view id);

struct ModuleConfig {
    bool enabled = false;
    std::string selectedProfile = "default";
    std::map<std::string, Values, std::less<>> profiles;
    Values& values();
    const Values& values() const;
    double number(std::string_view key) const;
    bool flag(std::string_view key) const;
    void number(std::string_view key, double value);
    void flag(std::string_view key, bool value);
};
struct Keybind {
    int key = 0;
    unsigned modifiers = 0;
    auto operator<=>(const Keybind&) const = default;
};
using Color = std::array<float, 4>;
struct UiConfig {
    int category = 0;
    std::array<int, 4> tabs{0,0,0,0};
    float scale = 1.f, hueSpeed = 4.f, rounding = 12.f;
    bool animateHue = true, animateBackground = true;
    Color accent{.38f,0.f,1.f,1.f};
    Color border{.125f,.125f,.125f,1.f}, primary{.071f,.071f,.071f,1.f};
    Color secondary{.102f,.102f,.102f,1.f}, text{.94f,.94f,.94f,1.f};
    Color disabled{.48f,.49f,.54f,1.f}, activeText{.04f,.04f,.04f,1.f};
    float huebar = .8f, huebarHeight = 2.f;
    bool footer = true, dpiAware = true, windowLocked = false;
    float textureOpacity = .65f, panelOpacity = .85f;
    float backgroundScale = 1.f, backgroundSpeed = .9f, backgroundX = 0.f, backgroundY = 0.f;
    bool scaleImage = true, accentTint = true;
    std::string imagePath;
};
struct ServiceConfig {
    std::string endpoint, publicKeyPath;
    bool required = false;
};
struct UpdateConfig {
    std::string feedUrl, publicKeyPath, signerThumbprint;
    std::string channel = "stable";
};
struct Profile {
    int schemaVersion = kSchemaVersion;
    UiConfig ui;
    std::map<std::string, ModuleConfig, std::less<>> modules;
    std::map<std::string, Keybind, std::less<>> bindings;
    std::array<int,6> movement{'W','A','S','D',0x11,0x10};
    ServiceConfig license;
    UpdateConfig updates;
    bool requireCapturedCursor = true;
};
Profile defaultProfile();
std::vector<std::string> validate(Profile& profile);
std::vector<std::string> bindingConflicts(const Profile& profile);
bool isModifierKey(int key);
bool isSafeBinding(const Keybind& key, bool global);

struct GameSnapshot {
    bool present = false, identityVerified = false, foreground = false, cursorCaptured = false;
    bool x64 = false;
    std::uint32_t pid = 0;
    std::uint64_t creationTime = 0;
    std::uintptr_t window = 0;
    std::string build, packageFamily, detail = "Looking for Minecraft";
    ConnectionState state = ConnectionState::LookingForGame;
};
struct ModuleHealth {
    ModuleState state = ModuleState::Disabled;
    SupportState support = SupportState::Unknown;
    std::string code, detail;
    bool enabledByUser = false;
};
struct RuntimeStatus {
    bool armed = false, licensed = true, inputHealthy = true;
    std::array<std::uint64_t,2> requested{}, sent{}, acknowledged{};
    std::uint64_t failures = 0;
    std::string stopReason = "Press F8 in Minecraft to arm input. F9 always stops.";
};
ModuleHealth computeHealth(const ModuleDefinition&, const ModuleConfig&, const GameSnapshot&, const RuntimeStatus&);
std::string_view stateName(ModuleState);
double timerTps(double multiplier);
double timerBoost(double multiplier);
double wrappedDegrees(double degrees);
struct Vec3 { double x=0, y=0, z=0; };
struct Orientation { double yaw=0, pitch=0; };
Orientation lookAt(Vec3 source, Vec3 target);
Orientation smoothOrientation(Orientation from, Orientation to, double degreesPerSecond, double deltaSeconds);
struct Target { std::uint64_t id=0; Vec3 position; bool self=false, friendTarget=false, visible=true, bot=false; };
struct TargetPolicy { double maxDistance=5, maxYaw=90, maxPitch=90; bool avoidWalls=true, filterBots=true; };
std::vector<std::uint64_t> eligibleTargets(Vec3 source, Orientation view,
    const std::vector<Target>& targets, const TargetPolicy& policy);
} // namespace aster
