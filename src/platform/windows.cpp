#include "platform/windows.h"
#include <TlHelp32.h>
#include <appmodel.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <mmsystem.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <memory>
#include <stdexcept>

namespace aster {
InputTimingSession::InputTimingSession(bool enabled){
    previous_.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    policySaved_=GetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&previous_,sizeof(previous_))!=FALSE;
    active(enabled);
}
InputTimingSession::~InputTimingSession(){active(false);}
void InputTimingSession::active(bool enabled){
    if(enabled==active_)return;
    if(enabled){
        if(policySaved_){auto policy=previous_;policy.ControlMask|=PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;policy.StateMask&=~PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&policy,sizeof(policy));}
        period_=timeBeginPeriod(1)==TIMERR_NOERROR;active_=true;
    }else {
        if(period_)timeEndPeriod(1);if(policySaved_)SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&previous_,sizeof(previous_));active_=period_=false;
    }
}
namespace {
constexpr ULONG_PTR kInputMarker=0x41535452;
struct Handle {
    HANDLE value=nullptr;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::string hex(const unsigned char* data,std::size_t length){
    constexpr char alphabet[]="0123456789abcdef";std::string result(length*2,'0');
    for(std::size_t i=0;i<length;++i){result[i*2]=alphabet[data[i]>>4];result[i*2+1]=alphabet[data[i]&15];}return result;
}
std::string normalizeHex(std::string_view value){
    std::string result;for(unsigned char c:value)if(!std::isspace(c)&&c!=':')result+=static_cast<char>(std::tolower(c));return result;
}
BOOL CALLBACK findChild(HWND child,LPARAM param){
    auto pair=reinterpret_cast<std::pair<DWORD,bool>*>(param);DWORD pid=0;GetWindowThreadProcessId(child,&pid);
    if(pid==pair->first){pair->second=true;return FALSE;}return TRUE;
}
HWND findGameWindow(DWORD pid){
    struct Search{DWORD pid;HWND result;};Search search{pid,nullptr};
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        auto* search=reinterpret_cast<Search*>(data);
        if(IsWindowVisible(window)&&windowBelongsTo(window,search->pid)){search->result=window;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));return search.result;
}
GameSnapshot inspect(DWORD pid){
    GameSnapshot g;g.present=true;g.pid=pid;g.state=ConnectionState::Detected;
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
    if(!process.value){g.detail="Minecraft candidate found; process identity unavailable.";return g;}
    std::wstring path(32768,L'\0');DWORD chars=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process.value,0,path.data(),&chars)){g.detail="Cannot verify game image path.";return g;}
    path.resize(chars);
    g.imagePath=utf8(path);
    if(_wcsicmp(std::filesystem::path(path).filename().c_str(),L"Minecraft.Windows.exe")!=0){g.detail="Unexpected game image.";return g;}
    UINT32 length=0;LONG code=GetPackageFamilyName(process.value,&length,nullptr);
    if(code!=ERROR_INSUFFICIENT_BUFFER){g.detail="Game is not the expected Microsoft Store package.";return g;}
    std::wstring family(length,L'\0');
    if(GetPackageFamilyName(process.value,&length,family.data())!=ERROR_SUCCESS){g.detail="Cannot resolve package identity.";return g;}
    family.resize(length?length-1:0);g.packageFamily=utf8(family);
    if(family!=L"Microsoft.MinecraftUWP_8wekyb3d8bbwe"){g.detail="Unsupported package family.";return g;}
    length=0;GetPackageFullName(process.value,&length,nullptr);std::wstring full(length,L'\0');
    if(length&&GetPackageFullName(process.value,&length,full.data())==ERROR_SUCCESS){
        UINT32 size=0;PackageIdFromFullName(full.c_str(),PACKAGE_INFORMATION_BASIC,&size,nullptr);
        std::vector<BYTE> storage(size);
        if(size&&PackageIdFromFullName(full.c_str(),PACKAGE_INFORMATION_BASIC,&size,storage.data())==ERROR_SUCCESS){
            auto package=reinterpret_cast<PACKAGE_ID*>(storage.data());
            const auto& v=package->version;
            g.build=std::to_string(v.Major)+"."+std::to_string(v.Minor)+"."+std::to_string(v.Build)+"."+std::to_string(v.Revision);
        }
    }
    USHORT processMachine=0,nativeMachine=0;
    if(IsWow64Process2(process.value,&processMachine,&nativeMachine))g.x64=processMachine==IMAGE_FILE_MACHINE_UNKNOWN&&nativeMachine==IMAGE_FILE_MACHINE_AMD64;
    FILETIME creation{},exit{},kernel{},user{};
    if(GetProcessTimes(process.value,&creation,&exit,&kernel,&user))g.creationTime=(static_cast<std::uint64_t>(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    g.identityVerified=!g.build.empty()&&g.creationTime!=0;
    auto hwnd=findGameWindow(pid);g.window=reinterpret_cast<std::uintptr_t>(hwnd);
    g.foreground=windowBelongsTo(GetForegroundWindow(),pid);g.cursorCaptured=cursorCapturedFor(hwnd);
    g.detail=!g.x64?"Architecture is unsupported":!g.identityVerified?"Build identity could not be verified":
        !hwnd?"Game identified; waiting for its window":verifiedInputBuild(g.build)?"Verified Store build detected; native adapter status is reported separately":
        "Untested game build; input and engine adapters are disabled";
    return g;
}
}
std::wstring wide(std::string_view s){
    if(s.empty())return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    if(!n)throw std::runtime_error("Invalid UTF-8");std::wstring r(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n);return r;
}
std::string utf8(std::wstring_view s){
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    if(!n)throw std::runtime_error("Invalid UTF-16");std::string r(n,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;
}
std::string keyName(const Keybind& key){
    if(!key.key)return "Not bound";
    std::string prefix;if(key.modifiers&2)prefix+="Ctrl+";if(key.modifiers&1)prefix+="Alt+";if(key.modifiers&4)prefix+="Shift+";
    if(key.key>=0x70&&key.key<=0x87)return prefix+"F"+std::to_string(key.key-0x70+1);
    if(key.key==VK_CONTROL||key.key==VK_LCONTROL||key.key==VK_RCONTROL)return prefix+"Ctrl";
    if(key.key==VK_SHIFT||key.key==VK_LSHIFT||key.key==VK_RSHIFT)return prefix+"Shift";
    if(key.key==VK_MENU||key.key==VK_LMENU||key.key==VK_RMENU)return prefix+"Alt";
    UINT scan=MapVirtualKeyW(key.key,MAPVK_VK_TO_VSC);LONG param=static_cast<LONG>(scan<<16);
    if(key.key==VK_LEFT||key.key==VK_RIGHT||key.key==VK_UP||key.key==VK_DOWN||key.key==VK_DELETE||key.key==VK_INSERT||key.key==VK_HOME||key.key==VK_END||key.key==VK_PRIOR||key.key==VK_NEXT)param|=1<<24;
    wchar_t name[80]{};if(GetKeyNameTextW(param,name,80))return prefix+utf8(name);
    return prefix+"VK "+std::to_string(key.key);
}
std::vector<unsigned char> sha256Bytes(std::string_view bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA-256 provider unavailable");
    DWORD objectSize=0,hashSize=0,size=0;
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&size,0);
    BCryptGetProperty(algorithm,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&hashSize),sizeof(hashSize),&size,0);
    std::vector<unsigned char> object(objectSize),result(hashSize);
    bool ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    if(ok)ok=bytes.size()<=MAXDWORD&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,result.data(),hashSize,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok)throw std::runtime_error("SHA-256 failed");return result;
}
std::string sha256(std::string_view bytes){auto data=sha256Bytes(bytes);return hex(data.data(),data.size());}
std::string randomHex(std::size_t length){
    if(length>1024)throw std::runtime_error("Random request too large");std::vector<unsigned char> data(length);
    if(BCryptGenRandom(nullptr,data.data(),static_cast<ULONG>(length),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Secure random generator failed");
    return hex(data.data(),data.size());
}
std::vector<unsigned char> base64Decode(std::string_view value){
    if(value.size()>4*1024*1024)throw std::runtime_error("Base64 input too large");
    std::string input(value);for(char& c:input){if(c=='-')c='+';if(c=='_')c='/';}
    while(input.size()%4)input+='=';
    DWORD length=0;
    if(!CryptStringToBinaryA(input.data(),static_cast<DWORD>(input.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&length,nullptr,nullptr))throw std::runtime_error("Invalid base64");
    std::vector<unsigned char> result(length);
    if(!CryptStringToBinaryA(input.data(),static_cast<DWORD>(input.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,result.data(),&length,nullptr,nullptr))throw std::runtime_error("Invalid base64");
    result.resize(length);return result;
}
bool verifyRsaSha256(std::string_view payload,std::string_view signature,std::string_view pem){
    try{
        if(pem.empty()||pem.size()>16384||signature.size()>2048)return false;
        DWORD length=0;
        if(!CryptStringToBinaryA(pem.data(),static_cast<DWORD>(pem.size()),CRYPT_STRING_BASE64HEADER,nullptr,&length,nullptr,nullptr))return false;
        std::vector<BYTE> der(length);
        if(!CryptStringToBinaryA(pem.data(),static_cast<DWORD>(pem.size()),CRYPT_STRING_BASE64HEADER,der.data(),&length,nullptr,nullptr))return false;
        CERT_PUBLIC_KEY_INFO* info=nullptr;DWORD size=0;
        if(!CryptDecodeObjectEx(X509_ASN_ENCODING,X509_PUBLIC_KEY_INFO,der.data(),length,CRYPT_DECODE_ALLOC_FLAG,nullptr,&info,&size))return false;
        BCRYPT_KEY_HANDLE key=nullptr;bool imported=CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,info,0,nullptr,&key)!=FALSE;LocalFree(info);
        if(!imported)return false;
        auto hash=sha256Bytes(payload),sig=base64Decode(signature);
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        bool valid=BCryptVerifySignature(key,&padding,hash.data(),static_cast<ULONG>(hash.size()),sig.data(),static_cast<ULONG>(sig.size()),BCRYPT_PAD_PKCS1)>=0;
        BCryptDestroyKey(key);return valid;
    }catch(...){return false;}
}
bool trustedExecutable(const std::filesystem::path& path,std::string_view required){
    auto expected=normalizeHex(required);if(expected.size()!=64)return false;
    WINTRUST_FILE_INFO file{sizeof(file),path.c_str(),nullptr,nullptr};
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;
    data.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;
    data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&policy,&data);
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&data);if(result!=ERROR_SUCCESS)return false;
    HCERTSTORE store=nullptr;HCRYPTMSG message=nullptr;DWORD encoding=0,type=0,format=0;
    if(!CryptQueryObject(CERT_QUERY_OBJECT_FILE,path.c_str(),CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,CERT_QUERY_FORMAT_FLAG_BINARY,0,&encoding,&type,&format,&store,&message,nullptr))return false;
    DWORD count=0;CryptMsgGetParam(message,CMSG_SIGNER_INFO_PARAM,0,nullptr,&count);
    std::vector<BYTE> buffer(count);bool valid=false;
    if(count&&CryptMsgGetParam(message,CMSG_SIGNER_INFO_PARAM,0,buffer.data(),&count)){
        auto signer=reinterpret_cast<CMSG_SIGNER_INFO*>(buffer.data());CERT_INFO ci{};ci.Issuer=signer->Issuer;ci.SerialNumber=signer->SerialNumber;
        auto cert=CertFindCertificateInStore(store,X509_ASN_ENCODING|PKCS_7_ASN_ENCODING,0,CERT_FIND_SUBJECT_CERT,&ci,nullptr);
        if(cert){BYTE digest[32];DWORD bytes=sizeof(digest);
            valid=CertGetCertificateContextProperty(cert,CERT_SHA256_HASH_PROP_ID,digest,&bytes)&&hex(digest,bytes)==expected;
            CertFreeCertificateContext(cert);}
    }
    CryptMsgClose(message);CertCloseStore(store,0);return valid;
}
void copyText(HWND window,std::string_view text){
    auto value=wide(text);if(!OpenClipboard(window))throw std::runtime_error("Clipboard is busy");
    HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,(value.size()+1)*sizeof(wchar_t));
    if(!block){CloseClipboard();throw std::runtime_error("Clipboard allocation failed");}
    auto memory=GlobalLock(block);if(!memory){GlobalFree(block);CloseClipboard();throw std::runtime_error("Clipboard lock failed");}
    memcpy(memory,value.c_str(),(value.size()+1)*sizeof(wchar_t));GlobalUnlock(block);EmptyClipboard();
    if(!SetClipboardData(CF_UNICODETEXT,block)){GlobalFree(block);CloseClipboard();throw std::runtime_error("Clipboard write failed");}CloseClipboard();
}
bool windowBelongsTo(HWND window,DWORD pid){
    if(!window||!pid||!IsWindow(window))return false;
    DWORD actual=0;GetWindowThreadProcessId(window,&actual);if(actual==pid)return true;
    std::pair<DWORD,bool> search{pid,false};EnumChildWindows(window,findChild,reinterpret_cast<LPARAM>(&search));return search.second;
}
bool cursorCapturedFor(HWND window){
    if(!window||!IsWindow(window))return false;
    RECT clip{},client{};if(!GetClipCursor(&clip)||!GetWindowRect(window,&client))return false;
    const int desktopW=GetSystemMetrics(SM_CXVIRTUALSCREEN),desktopH=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(clip.right-clip.left>=desktopW&&clip.bottom-clip.top>=desktopH)return false;
    RECT intersection{};
    return IntersectRect(&intersection,&clip,&client)&&clip.left>=client.left-1&&clip.top>=client.top-1&&clip.right<=client.right+1&&clip.bottom<=client.bottom+1;
}
bool gameplayCaptureReady(const GameSnapshot& game,const EngineRuntime& engine){
    return game.cursorCaptured||(engine.bound&&engine.worldReady&&engine.heartbeatAge<500&&engine.screen=="gameplay_captured");
}
bool verifiedInputBuild(std::string_view build){return build==kInputResearchBuild;}
GameSnapshot GameDetector::detectOnce(){
    Handle list(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(list.value==INVALID_HANDLE_VALUE){
        GameSnapshot g;g.state=ConnectionState::Failed;g.detail="Process enumeration failed; automatic retry is active.";return g;}
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);GameSnapshot candidate;
    if(Process32FirstW(list.value,&entry))do{
        if(_wcsicmp(entry.szExeFile,L"Minecraft.Windows.exe")!=0)continue;
        auto game=inspect(entry.th32ProcessID);
        if(game.identityVerified&&game.foreground)return game;
        if(!candidate.present||(!candidate.identityVerified&&game.identityVerified))candidate=std::move(game);
    }while(Process32NextW(list.value,&entry));return candidate;
}
GameDetector::GameDetector(Diagnostics& log):log_(log),worker_([this](std::stop_token token){
    std::string previous;unsigned absent=0;
    while(!token.stop_requested()){
        auto game=detectOnce();auto identity=std::to_string(game.pid)+":"+std::to_string(game.creationTime)+":"+game.build;
        if(identity!=previous){log_.event("detector",game.present?"GAME_DETECTED":"GAME_NOT_RUNNING",game.detail,"lifecycle",game.build);previous=identity;}
        bool present=game.present;{std::lock_guard lock(mutex_);current_=std::move(game);}
        absent=present?0:std::min(absent+1,3u);
        auto delay=std::chrono::milliseconds(present?1000:(500u<<absent));
        std::unique_lock lock(mutex_);wake_.wait_for(lock,token,delay,[]{return false;});
    }
}){}
GameDetector::~GameDetector(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();}
GameSnapshot GameDetector::snapshot()const{
    GameSnapshot g;{std::lock_guard lock(mutex_);g=current_;}
    if(g.present){auto window=reinterpret_cast<HWND>(g.window);
        g.foreground=windowBelongsTo(GetForegroundWindow(),g.pid);
        g.cursorCaptured=g.foreground&&cursorCapturedFor(window);
        if(!IsWindow(window))g.foreground=false;
    }return g;
}
InputObserver* InputObserver::instance_=nullptr;
InputObserver::InputObserver(){
    if(instance_)throw std::runtime_error("Only one physical input observer is permitted");instance_=this;
    mouseHook_=SetWindowsHookExW(WH_MOUSE_LL,mouseProc,GetModuleHandleW(nullptr),0);
    keyboardHook_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboardProc,GetModuleHandleW(nullptr),0);
}
InputObserver::~InputObserver(){if(mouseHook_)UnhookWindowsHookEx(mouseHook_);if(keyboardHook_)UnhookWindowsHookEx(keyboardHook_);instance_=nullptr;}
LRESULT CALLBACK InputObserver::mouseProc(int code,WPARAM message,LPARAM data){
    if(code==HC_ACTION&&instance_){
        auto event=reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        if(!(event->flags&LLMHF_INJECTED)){
            int button=-1;bool down=false;
            if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP){button=0;down=message==WM_LBUTTONDOWN;}
            if(message==WM_RBUTTONDOWN||message==WM_RBUTTONUP){button=1;down=message==WM_RBUTTONDOWN;}
            if(button>=0){std::lock_guard lock(instance_->mutex_);auto& s=instance_->state_;
                if(down&&!s.mouse[button])s.pressSequence[button]=++s.eventSequence;
                s.mouse[button]=down;}
        }
    }return CallNextHookEx(nullptr,code,message,data);
}
LRESULT CALLBACK InputObserver::keyboardProc(int code,WPARAM message,LPARAM data){
    if(code==HC_ACTION&&instance_){
        auto event=reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        if((!(event->flags&LLKHF_INJECTED)||((event->vkCode==VK_F8||event->vkCode==VK_F9)&&event->dwExtraInfo!=kInputMarker))&&event->vkCode<256){
            int key=static_cast<int>(event->vkCode);bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
            if(key==VK_SHIFT)key=static_cast<int>(MapVirtualKeyW(event->scanCode,MAPVK_VSC_TO_VK_EX));
            if(key==VK_CONTROL)key=event->flags&LLKHF_EXTENDED?VK_RCONTROL:VK_LCONTROL;
            if(key==VK_MENU)key=event->flags&LLKHF_EXTENDED?VK_RMENU:VK_LMENU;
            std::lock_guard lock(instance_->mutex_);auto& s=instance_->state_;bool fresh=down&&!s.keys[key];s.keys[key]=down;
            s.keys[VK_SHIFT]=s.keys[VK_LSHIFT]||s.keys[VK_RSHIFT];
            s.keys[VK_CONTROL]=s.keys[VK_LCONTROL]||s.keys[VK_RCONTROL];s.keys[VK_MENU]=s.keys[VK_LMENU]||s.keys[VK_RMENU];
            if(fresh){
                ++s.eventSequence;unsigned modifiers=(s.keys[VK_MENU]?1u:0u)|(s.keys[VK_CONTROL]?2u:0u)|(s.keys[VK_SHIFT]?4u:0u);
                if(instance_->presses_.size()<128)instance_->presses_.push_back({key,modifiers,s.eventSequence});
                if(key=='T'||key==VK_RETURN||key==VK_ESCAPE||key==VK_OEM_2||key==VK_F9){++s.pauseSequence;s.pauseKey=key;}
            }
        }
    }return CallNextHookEx(nullptr,code,message,data);
}
PhysicalInput InputObserver::snapshot()const{std::lock_guard lock(mutex_);return state_;}
std::vector<KeyPress> InputObserver::drainPresses(){std::lock_guard lock(mutex_);std::vector<KeyPress> result;result.swap(presses_);return result;}
bool InputService::sendEvent(const OutputEvent& event){
    if(event.kind==OutputKind::MouseDown||event.kind==OutputKind::MouseUp){INPUT e{};e.type=INPUT_MOUSE;e.mi.dwExtraInfo=kInputMarker;e.mi.dwFlags=event.code?(event.kind==OutputKind::MouseDown?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP):(event.kind==OutputKind::MouseDown?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP);return SendInput(1,&e,sizeof(e))==1;}
    if(event.kind==OutputKind::Click){
        INPUT events[3]{};const DWORD down=event.code?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_LEFTDOWN;
        const DWORD up=event.code?MOUSEEVENTF_RIGHTUP:MOUSEEVENTF_LEFTUP;
        for(auto& e:events){e.type=INPUT_MOUSE;e.mi.dwExtraInfo=kInputMarker;}
        events[0].mi.dwFlags=up;events[1].mi.dwFlags=down;events[2].mi.dwFlags=up;
        UINT sent=SendInput(3,events,sizeof(INPUT));
        if(sent!=3){INPUT release=events[2];SendInput(1,&release,sizeof(INPUT));return false;}return true;
    }
    INPUT e{};e.type=INPUT_KEYBOARD;e.ki.dwExtraInfo=kInputMarker;
    UINT scan=MapVirtualKeyW(event.code,MAPVK_VK_TO_VSC_EX);e.ki.wScan=static_cast<WORD>(scan&255u);
    e.ki.dwFlags=KEYEVENTF_SCANCODE|(event.kind==OutputKind::KeyUp?KEYEVENTF_KEYUP:0u)|((scan&0xFF00u)?KEYEVENTF_EXTENDEDKEY:0u);
    return e.ki.wScan&&SendInput(1,&e,sizeof(INPUT))==1;
}
InputService::InputService(GameDetector& d,InputObserver& o,Diagnostics& l):detector_(d),observer_(o),log_(l),profile_(std::make_shared<const Profile>(defaultProfile())),engine_(d,l),worker_([this](std::stop_token token){
    InputPlanner planner;InputTimingSession timing(false);std::array<bool,2> mining{};auto start=std::chrono::steady_clock::now();std::uint64_t pause=observer_.snapshot().pauseSequence;
    Handle timer(CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS));
    if(!timer.value)timer.value=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    while(!token.stop_requested()){
        std::shared_ptr<const Profile> snapshot;RuntimeStatus status;std::uint64_t worldToken;bool restoreForward;{std::lock_guard lock(mutex_);snapshot=profile_;status=status_;worldToken=worldToken_;restoreForward=restoreForward_;}const auto& config=*snapshot;
        auto physical=observer_.snapshot();auto game=detector_.snapshot();
        auto engine=engine_.status();
        timing.active(status.armed);
        if(status.armed&&engine.world!=worldToken){stop("Minecraft world changed. Press F8 in-world to resume.");status.armed=false;}
        if(status.armed&&(!engine.worldReady||engine.screen!="gameplay_captured")){stop("Minecraft world or screen changed. Press F8 in-world to resume.");status.armed=false;}
        if(status.armed&&physical.pauseSequence!=pause){stop("Input paused for chat/menu key. Press F8 in-world to resume.");status.armed=false;}
        const bool screenInterrupted=physical.pauseSequence!=pause&&physical.pauseKey!=VK_F9;
        pause=physical.pauseSequence;
        if(status.armed&&(!game.foreground||!game.identityVerified||!game.x64||!verifiedInputBuild(game.build))){
            stop("Focus or game identity changed. Press F8 in-world to resume.");status.armed=false;}
        if(status.armed&&config.requireCapturedCursor&&!gameplayCaptureReady(game,engine)){stop("Minecraft released gameplay capture. Press F8 in-world to resume.");status.armed=false;}
        InputGate gate{status.armed,game.identityVerified&&game.x64&&verifiedInputBuild(game.build)&&game.window!=0,
                       game.foreground,!config.requireCapturedCursor||gameplayCaptureReady(game,engine),status.licensed&&status.inputHealthy&&observer_.healthy()};
        double now=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        InputContext context{engine.worldReady&&engine.screen=="gameplay_captured",engine.blockTarget,engine.entityTarget,engine.blockToken,engine.attackCalls};
        context.restorePhysical=restoreForward&&!screenInterrupted&&game.foreground&&context.worldReady&&engine.world==worldToken;
        auto events=planner.tick(now,config,physical,gate,context);
        for(const auto& event:events){
            if(event.kind!=OutputKind::KeyUp&&event.kind!=OutputKind::MouseUp){
                auto fresh=detector_.snapshot();bool stillArmed;{std::lock_guard lock(mutex_);stillArmed=status_.armed&&status_.licensed;}
                if((!stillArmed&&!(event.module=="physical_restore"&&context.restorePhysical))||!fresh.foreground||(!gameplayCaptureReady(fresh,engine_.status())&&config.requireCapturedCursor))break;
            }
            if(event.kind==OutputKind::Click){std::lock_guard lock(mutex_);++status_.requested[event.code];}
            const bool native=(event.kind==OutputKind::Click||event.kind==OutputKind::MouseDown||event.kind==OutputKind::MouseUp)&&engine.bound;
            if(native&&event.kind==OutputKind::MouseDown)mining[event.code]=true;
            if(event.kind==OutputKind::MouseUp)mining[event.code]=false;
            bool ok=native||sendEvent(event);
            {std::lock_guard lock(mutex_);
                if(ok&&event.kind==OutputKind::Click){if(native)++status_.queued[event.code];else ++status_.sent[event.code];}
                if(ok&&event.kind==OutputKind::KeyDown){const char* ids[]={"walk","sprint","stap"};for(unsigned n=0;n<3;++n)if(event.module==ids[n])++keyRequests_[n];}
                if(!ok){++status_.failures;status_.inputHealthy=false;status_.armed=false;status_.stopReason="Windows rejected an input event; paused.";}}
            if(!ok){log_.event(event.module,"INPUT_SEND_FAILED","Windows rejected synthesized input; scheduler paused.");
                for(const auto& release:planner.stop(observer_.snapshot()))sendEvent(release);break;}
        }
        {std::lock_guard lock(mutex_);
            keyApplied_={config.modules.at("walk").enabled&&planner.ownsKey(config.movement[0]),config.modules.at("sprint").enabled&&planner.ownsKey(config.movement[4]),config.modules.at("stap").enabled&&planner.ownsKey(config.movement[2])};
            keyConditions_={config.modules.at("walk").enabled,config.modules.at("sprint").enabled&&(physical.keys[config.movement[0]]||config.modules.at("walk").enabled)&&!keyApplied_[2],config.modules.at("stap").enabled&&physical.keys[config.movement[0]]&&!physical.keys[config.movement[2]]};
            engine_.input(physical,status_.armed,status_.licensed,status_.queued,mining,worldToken_);status=status_;}
        const auto requested=config.modules.at("clicker").number("tuner");
        const bool lowCpuZoom=config.modules.at("zoom").enabled&&config.modules.at("zoom").flag("lowCpuMode")&&!config.modules.at("clicker").enabled&&!config.modules.at("stap").enabled&&!config.modules.at("walk").enabled&&!config.modules.at("sprint").enabled;
        const double hz=status.armed?(lowCpuZoom?100.:(requested>0?std::clamp(requested,50.,1000.):1000.)):60.;
        if(timer.value){LARGE_INTEGER due;due.QuadPart=-static_cast<LONGLONG>(10000000./hz);if(SetWaitableTimerEx(timer.value,&due,0,nullptr,nullptr,nullptr,0))WaitForSingleObject(timer.value,25);else Sleep(1);}
        else {std::unique_lock lock(mutex_);wake_.wait_for(lock,token,std::chrono::milliseconds(status_.armed?1:16),[]{return false;});}
    }
    for(const auto& release:planner.stop(observer_.snapshot()))sendEvent(release);
}){engine_.configure(*profile_);}
InputService::~InputService(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();}
void InputService::configure(const Profile& p){auto next=std::make_shared<const Profile>(p);{std::lock_guard lock(mutex_);profile_=std::move(next);wake_.notify_all();}engine_.configure(p);}
bool InputService::arm(){
    auto g=detector_.snapshot();std::lock_guard lock(mutex_);
    if(!g.foreground||!g.identityVerified||!g.x64||!verifiedInputBuild(g.build)||!g.window){status_.stopReason="Switch to a verified Minecraft build and press F8 in-world.";return false;}
    auto engine=engine_.status();if(!engine.worldReady||engine.screen!="gameplay_captured"){status_.stopReason=engine.detail;return false;}
    if(profile_->requireCapturedCursor&&!gameplayCaptureReady(g,engine)){status_.stopReason="Resume a private world with native gameplay capture, then press F8.";return false;}
    if(!status_.licensed||!observer_.healthy()){status_.stopReason="Input observer or own-service entitlement unavailable.";return false;}
    worldToken_=engine.world;restoreForward_=false;status_.armed=true;status_.inputHealthy=true;status_.stopReason="Input armed for this Minecraft world.";wake_.notify_all();
    log_.event("input","INPUT_ARMED","User armed foreground-gated input for private-world validation.");return true;
}
void InputService::stop(std::string reason,bool restoreForward){
    bool wasArmed;{std::lock_guard lock(mutex_);wasArmed=status_.armed;status_.armed=false;restoreForward_=restoreForward;status_.stopReason=std::move(reason);wake_.notify_all();}
    if(wasArmed)log_.event("input","INPUT_STOPPED","Input canceled; owned keys are being released.");
}
void InputService::setLicensed(bool value){std::lock_guard lock(mutex_);status_.licensed=value;if(!value){status_.armed=false;status_.stopReason="Own-service license unavailable.";}wake_.notify_all();}
RuntimeStatus InputService::status()const{
    RuntimeStatus result;std::array<std::uint64_t,3> requests;std::array<bool,3> applied,conditions;std::shared_ptr<const Profile> profile;
    {std::lock_guard lock(mutex_);result=status_;requests=keyRequests_;applied=keyApplied_;conditions=keyConditions_;profile=profile_;}
    result.engine=engine_.status();const char* ids[]={"walk","sprint","stap"};
    for(unsigned n=0;n<3;++n){auto& evidence=result.engine.modules[moduleIndex(ids[n])];evidence.bound=evidence.bound&&observer_.healthy();evidence.requested=profile->modules.at(ids[n]).enabled;
        evidence.conditions=conditions[n]&&result.armed&&result.engine.permitted;evidence.applied=applied[n]&&evidence.conditions&&result.inputHealthy;evidence.requests=requests[n];evidence.observations=0;
        evidence.reason=!evidence.bound?"Input handler or native world adapter unavailable":!result.armed||!result.engine.permitted?result.stopReason:!evidence.conditions?"Waiting for physical movement/attack conditions":evidence.applied?"Configured Windows key held; game motion is measured separately":"Input handler ready; Windows output and game motion are separate evidence";}
    return result;
}
Hotkeys::Hotkeys(HWND window):window_(window){panicOk_=RegisterHotKey(window_,PanicId,MOD_NOREPEAT,VK_F9)!=FALSE;}
Hotkeys::~Hotkeys(){if(hideRegistered_)UnregisterHotKey(window_,HideId);if(panicOk_)UnregisterHotKey(window_,PanicId);}
bool Hotkeys::configureHide(const Keybind& key){
    if(hideRegistered_){UnregisterHotKey(window_,HideId);hideRegistered_=false;}
    if(!key.key)return true;if(!isSafeBinding(key,true))return false;
    hideRegistered_=RegisterHotKey(window_,HideId,key.modifiers|MOD_NOREPEAT,key.key)!=FALSE;return hideRegistered_;
}
} // namespace aster
