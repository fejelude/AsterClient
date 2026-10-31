#pragma once
#include <Windows.h>
#include "core/planner.h"
#include "diagnostics/log.h"
#include "integration/engine.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace aster {
class InputTimingSession {
public:
    explicit InputTimingSession(bool active=true);
    ~InputTimingSession();
    void active(bool);
private:
    bool active_=false,period_=false,policySaved_=false;
    PROCESS_POWER_THROTTLING_STATE previous_{};
    InputTimingSession(const InputTimingSession&)=delete;
    InputTimingSession& operator=(const InputTimingSession&)=delete;
};
std::wstring wide(std::string_view text);
std::string utf8(std::wstring_view text);
std::string keyName(const Keybind& key);
std::string sha256(std::string_view bytes);
std::vector<unsigned char> sha256Bytes(std::string_view bytes);
std::string randomHex(std::size_t byteCount);
std::vector<unsigned char> base64Decode(std::string_view text);
bool verifyRsaSha256(std::string_view payload,std::string_view signature,std::string_view publicPem);
bool trustedExecutable(const std::filesystem::path& path,std::string_view requiredThumbprint);
void copyText(HWND window,std::string_view text);
bool windowBelongsTo(HWND window,DWORD pid);
bool cursorCapturedFor(HWND window);
bool gameplayCaptureReady(const GameSnapshot&,const EngineRuntime&);
bool verifiedInputBuild(std::string_view build);

class GameDetector {
public:
    explicit GameDetector(Diagnostics&);
    ~GameDetector();
    GameSnapshot snapshot()const;
    static GameSnapshot detectOnce();
private:
    Diagnostics& log_;
    mutable std::mutex mutex_;
    GameSnapshot current_;
    std::condition_variable_any wake_;
    std::jthread worker_;
};
struct KeyPress {int key=0;unsigned modifiers=0;std::uint64_t sequence=0;};
class InputObserver {
public:
    InputObserver();
    ~InputObserver();
    bool healthy()const{return mouseHook_&&keyboardHook_;}
    PhysicalInput snapshot()const;
    std::vector<KeyPress> drainPresses();
private:
    static LRESULT CALLBACK mouseProc(int,WPARAM,LPARAM);
    static LRESULT CALLBACK keyboardProc(int,WPARAM,LPARAM);
    static InputObserver* instance_;
    HHOOK mouseHook_=nullptr,keyboardHook_=nullptr;
    mutable std::mutex mutex_;
    PhysicalInput state_;
    std::vector<KeyPress> presses_;
};
class InputService {
public:
    InputService(GameDetector&,InputObserver&,Diagnostics&);
    ~InputService();
    void configure(const Profile&);
    bool arm();
    void stop(std::string reason,bool restoreForward=false);
    void setLicensed(bool);
    RuntimeStatus status()const;
    static bool sendEvent(const OutputEvent& event);
private:
    GameDetector& detector_;
    InputObserver& observer_;
    Diagnostics& log_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const Profile> profile_;
    RuntimeStatus status_;
    std::uint64_t worldToken_=0;
    bool restoreForward_=false;
    std::array<std::uint64_t,3> keyRequests_{};
    std::array<bool,3> keyApplied_{},keyConditions_{};
    EngineService engine_;
    std::jthread worker_;
};

class Hotkeys {
public:
    explicit Hotkeys(HWND);
    ~Hotkeys();
    bool configureHide(const Keybind&);
    bool panicRegistered()const{return panicOk_;}
    static constexpr int HideId=0xA501,PanicId=0xA502;
private:
    HWND window_;
    bool hideRegistered_=false,panicOk_=false;
};
} // namespace aster
