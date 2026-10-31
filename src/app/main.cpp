#include "ui/interface.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <Windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <chrono>
#include <fstream>
#include <memory>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace aster;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace {
struct Graphics {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11RenderTargetView> target;
    bool software=false;
    void clear(){target.Reset();swap.Reset();context.Reset();device.Reset();}
    bool renderTarget(){
        ComPtr<ID3D11Texture2D> buffer;if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(&buffer))))return false;
        return SUCCEEDED(device->CreateRenderTargetView(buffer.Get(),nullptr,&target));
    }
    bool create(HWND window){
        clear();DXGI_SWAP_CHAIN_DESC description{};description.BufferCount=2;
        description.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.OutputWindow=window;description.SampleDesc.Count=1;description.Windowed=TRUE;
        description.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        constexpr D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0};D3D_FEATURE_LEVEL level;
        auto hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,2,D3D11_SDK_VERSION,&description,&swap,&device,&level,&context);
        software=FAILED(hr);
        if(software)hr=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,levels,2,D3D11_SDK_VERSION,&description,&swap,&device,&level,&context);
        return SUCCEEDED(hr)&&renderTarget();
    }
};
struct WindowContext {
    Profile* profile=nullptr;
    InputService* input=nullptr;
    UINT width=0,height=0;
    float dpiScale=1;
    bool quit=false,hidden=false;
} windowContext;
struct WindowLifetime {
    HWND window;
    ~WindowLifetime(){windowContext.input=nullptr;windowContext.profile=nullptr;if(IsWindow(window))DestroyWindow(window);}
};
struct ImGuiLifetime {
    bool win32=false,dx11=false;
    ~ImGuiLifetime(){if(dx11)ImGui_ImplDX11_Shutdown();if(win32)ImGui_ImplWin32_Shutdown();if(ImGui::GetCurrentContext())ImGui::DestroyContext();}
};
struct RuntimePointerLifetime {
    InputService& input;
    ~RuntimePointerLifetime(){input.stop("Client services are shutting down.");windowContext.input=nullptr;}
};
struct InstanceGuard {
    HANDLE mutex=nullptr;
    ~InstanceGuard(){if(mutex)CloseHandle(mutex);}
};
constexpr UINT TrayMessage=WM_APP+42;
void restore(HWND window){ShowWindow(window,SW_SHOW);ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);windowContext.hidden=false;}
LRESULT CALLBACK windowProc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_HOTKEY){
        if(wp==Hotkeys::PanicId&&windowContext.input)windowContext.input->stop("Emergency F9 stop.",true);
        if(wp==Hotkeys::HideId){if(IsWindowVisible(window)){ShowWindow(window,SW_HIDE);windowContext.hidden=true;}else restore(window);}return 0;
    }
    if(msg==TrayMessage){
        if(lp==WM_LBUTTONUP||lp==WM_LBUTTONDBLCLK)restore(window);
        if(lp==WM_RBUTTONUP){POINT point;GetCursorPos(&point);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Restore Aster");AppendMenuW(menu,MF_STRING,2,L"Stop input");AppendMenuW(menu,MF_STRING,3,L"Quit");
            SetForegroundWindow(window);int selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,window,nullptr);DestroyMenu(menu);
            if(selected==1)restore(window);if(selected==2&&windowContext.input)windowContext.input->stop("Stopped from tray menu.");if(selected==3)PostMessageW(window,WM_CLOSE,0,0);
        }return 0;
    }
    if(msg==WM_NCCALCSIZE&&wp)return 0;
    if(msg==WM_NCHITTEST){
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&pt);RECT client;GetClientRect(window,&client);
        bool locked=windowContext.profile&&windowContext.profile->ui.windowLocked;
        int border=static_cast<int>(6*windowContext.dpiScale);
        if(!locked){
            bool l=pt.x<border,r=pt.x>=client.right-border,t=pt.y<border,b=pt.y>=client.bottom-border;
            if(t&&l)return HTTOPLEFT;if(t&&r)return HTTOPRIGHT;if(b&&l)return HTBOTTOMLEFT;if(b&&r)return HTBOTTOMRIGHT;
            if(l)return HTLEFT;if(r)return HTRIGHT;if(t)return HTTOP;if(b)return HTBOTTOM;
            float scale=windowContext.dpiScale*(windowContext.profile?windowContext.profile->ui.scale:1.f);
            if(pt.y<50*scale&&pt.x<client.right-90*scale)return HTCAPTION;
        }return HTCLIENT;
    }
    if(ImGui::GetCurrentContext()&&ImGui_ImplWin32_WndProcHandler(window,msg,wp,lp))return 1;
    switch(msg){
        case WM_SIZE:if(wp!=SIZE_MINIMIZED){windowContext.width=LOWORD(lp);windowContext.height=HIWORD(lp);}return 0;
        case WM_DPICHANGED:{windowContext.dpiScale=HIWORD(wp)/96.f;auto rect=reinterpret_cast<RECT*>(lp);
            SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
        case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(lp);info->ptMinTrackSize={static_cast<LONG>(680*windowContext.dpiScale),static_cast<LONG>(560*windowContext.dpiScale)};return 0;}
        case WM_SYSCOMMAND:if((wp&0xfff0)==SC_KEYMENU)return 0;break;
        case WM_CLOSE:if(windowContext.input)windowContext.input->stop("Client is closing.");windowContext.quit=true;return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
void centerWindow(HWND window,float dpi){
    RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int width=std::min(static_cast<int>(820*dpi),static_cast<int>(work.right-work.left)),height=std::min(static_cast<int>(720*dpi),static_cast<int>(work.bottom-work.top));
    SetWindowPos(window,nullptr,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
}
struct Tray {
    NOTIFYICONDATAW data{};
    explicit Tray(HWND window){data.cbSize=sizeof(data);data.hWnd=window;data.uID=1;data.uFlags=NIF_ICON|NIF_TIP|NIF_MESSAGE;
        data.uCallbackMessage=TrayMessage;data.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101));
        if(!data.hIcon)data.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
        wcscpy_s(data.szTip,L"Aster Client - F9 stops input; click to restore");Shell_NotifyIconW(NIM_ADD,&data);}
    ~Tray(){Shell_NotifyIconW(NIM_DELETE,&data);}
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int exitCode=0;
    try{
        auto configPath=defaultConfigPath();unsigned smokeSeconds=0;std::filesystem::path diagnosticPath;int category=-1,tab=-1;
        int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
        for(int i=1;i<count;++i){
            if(std::wstring_view(args[i])==L"--profile"&&i+1<count)configPath=args[++i];
            else if(std::wstring_view(args[i])==L"--diagnose"&&i+1<count)diagnosticPath=args[++i];
            else if(std::wstring_view(args[i])==L"--smoke-seconds"&&i+1<count)smokeSeconds=static_cast<unsigned>(std::clamp(_wtoi(args[++i]),1,60));
            else if(std::wstring_view(args[i])==L"--page"&&i+2<count){category=_wtoi(args[++i]);tab=_wtoi(args[++i]);}
        }LocalFree(args);
        ConfigStore store(configPath);auto load=store.load();auto profile=std::move(load.profile);
        auto appDirectory=configPath.parent_path().parent_path();Diagnostics log(appDirectory/L"logs");
        if(!diagnosticPath.empty()){
            atomicWrite(diagnosticPath,log.report(profile,GameDetector::detectOnce(),{}),false);
            if(SUCCEEDED(com))CoUninitialize();return 0;
        }
        InstanceGuard instanceGuard;instanceGuard.mutex=CreateMutexW(nullptr,FALSE,L"Local\\AsterClient.Gui.Instance.v1");
        if(!instanceGuard.mutex)throw std::runtime_error("Could not create the single-instance guard.");
        if(GetLastError()==ERROR_ALREADY_EXISTS){if(auto existing=FindWindowW(L"AsterNativeClient",nullptr))restore(existing);if(SUCCEEDED(com))CoUninitialize();return 0;}
        if(category>=0&&category<4){profile.ui.category=category;profile.ui.tabs[category]=tab;validate(profile);}
        log.event("app","APP_STARTED","Original native Windows client initialized.");for(const auto& warning:load.warnings)log.event("config","CONFIG_RECOVERY",warning);
        if(profile.ui.dpiAware)SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
        windowContext.profile=&profile;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.style=CS_CLASSDC;wc.lpfnWndProc=windowProc;wc.hInstance=instance;
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101));wc.hIconSm=wc.hIcon;wc.lpszClassName=L"AsterNativeClient";
        if(!RegisterClassExW(&wc))throw std::runtime_error("Native window class creation failed.");
        HWND window=CreateWindowW(wc.lpszClassName,L"Aster Client | Native Bedrock Lab",WS_POPUP|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,
            CW_USEDEFAULT,CW_USEDEFAULT,820,720,nullptr,nullptr,instance,nullptr);
        if(!window)throw std::runtime_error("Native window creation failed.");
        WindowLifetime windowLifetime{window};
        windowContext.dpiScale=GetDpiForWindow(window)/96.f;centerWindow(window,windowContext.dpiScale);
        BOOL dark=TRUE;DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
        DWM_WINDOW_CORNER_PREFERENCE rounding=DWMWCP_ROUND;DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&rounding,sizeof(rounding));
        Graphics graphics;if(!graphics.create(window))throw std::runtime_error("DirectX 11 hardware and software rendering initialization failed.");
        IMGUI_CHECKVERSION();ImGui::CreateContext();ImGuiLifetime imguiLifetime;auto& io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;io.IniFilename=nullptr;io.LogFilename=nullptr;
        wchar_t windowsDirectory[MAX_PATH]{};GetWindowsDirectoryW(windowsDirectory,MAX_PATH);
        auto font=std::filesystem::path(windowsDirectory)/L"Fonts"/L"segoeui.ttf";
        if(std::filesystem::exists(font))io.Fonts->AddFontFromFileTTF(utf8(font.wstring()).c_str(),32.f);
        else {ImFontConfig fontConfig;fontConfig.SizePixels=32;io.Fonts->AddFontDefault(&fontConfig);}
        imguiLifetime.win32=ImGui_ImplWin32_Init(window);imguiLifetime.dx11=ImGui_ImplDX11_Init(graphics.device.Get(),graphics.context.Get());
        if(!imguiLifetime.win32||!imguiLifetime.dx11)throw std::runtime_error("Native UI backend initialization failed.");
        ShowWindow(window,SW_SHOW);UpdateWindow(window);
        {
            GameDetector detector(log);InputObserver observer;InputService input(detector,observer,log);LicenseClient license(profile.license,log);Hotkeys hotkeys(window);Tray tray(window);
            windowContext.input=&input;
            Interface ui(window,graphics.device.Get(),profile,store,log,detector,input,license,hotkeys);
            RuntimePointerLifetime runtimeLifetime{input};
            if(!hotkeys.configureHide(profile.bindings.at("hide")))ui.message="Hide hotkey could not be registered. Use the tray icon to restore the app.";
            if(!observer.healthy())ui.message="Physical input observation is unavailable; automation will remain paused.";
            if(load.readOnly)ui.message="This profile is read-only to preserve its original data.";
            auto started=std::chrono::steady_clock::now();auto nextFrame=started;
            auto recreate=[&]{
                input.stop("Graphics renderer is being recreated.");ui.save();ui.deviceChanged(nullptr);ImGui_ImplDX11_Shutdown();imguiLifetime.dx11=false;
                if(!graphics.create(window))throw std::runtime_error("Renderer recovery failed.");
                imguiLifetime.dx11=ImGui_ImplDX11_Init(graphics.device.Get(),graphics.context.Get());
                if(!imguiLifetime.dx11)throw std::runtime_error("Renderer UI backend recovery failed.");ui.deviceChanged(graphics.device.Get());
                log.event("graphics","RENDERER_RECREATED",graphics.software?"Software DirectX 11 renderer initialized.":"Hardware DirectX 11 renderer initialized.");
            };
            while(!windowContext.quit){
                MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);if(msg.message==WM_QUIT)windowContext.quit=true;}
                if(windowContext.quit)break;
                auto game=detector.snapshot();bool ownForeground=GetForegroundWindow()==window;
                for(const auto& press:observer.drainPresses()){
                    if(ownForeground&&ui.capturingKey()){ui.acceptKey(press);continue;}
                    if(press.key==VK_F9){input.stop("Emergency F9 stop.",true);continue;}
                    if(!game.foreground)continue;
                    if(press.key==VK_F8){if(input.status().armed)input.stop("Input session disarmed with F8.",true);else input.arm();continue;}
                    if(isModifierKey(press.key))continue;
                    std::vector<std::string> matches;
                    for(const auto& [id,key]:profile.bindings)if(id!="hide"&&bindingMatches(key,press.key,press.modifiers))matches.push_back(id);
                    if(matches.size()==1){auto& m=profile.modules.at(matches[0]);m.enabled=!m.enabled;ui.changed();}
                    else if(matches.size()>1)log.event("input","HOTKEY_CONFLICT","A conflicting module binding was ignored.");
                }
                auto entitlement=license.snapshot();
                input.setLicensed(!profile.license.required||(license.configuredFor(profile.license)&&entitlement.state!=LicenseState::LocalDevelopment&&entitlement.entitled()));
                if(windowContext.width&&windowContext.height){
                    graphics.target.Reset();auto hr=graphics.swap->ResizeBuffers(0,windowContext.width,windowContext.height,DXGI_FORMAT_UNKNOWN,0);
                    windowContext.width=windowContext.height=0;if(FAILED(hr)||!graphics.renderTarget())recreate();
                }
                if(smokeSeconds&&std::chrono::steady_clock::now()-started>std::chrono::seconds(smokeSeconds))break;
                if(!IsWindowVisible(window)||IsIconic(window)){MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);continue;}
                if(std::chrono::steady_clock::now()<nextFrame){
                    auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame-std::chrono::steady_clock::now()).count();
                    MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(std::max<std::int64_t>(1,remaining)),QS_ALLINPUT,MWMO_INPUTAVAILABLE);continue;
                }
                ImGui_ImplDX11_NewFrame();ImGui_ImplWin32_NewFrame();ImGui::NewFrame();ui.render(windowContext.dpiScale);ImGui::Render();
                constexpr float clear[]={.01f,.01f,.02f,1};auto target=graphics.target.Get();graphics.context->OMSetRenderTargets(1,&target,nullptr);
                graphics.context->ClearRenderTargetView(target,clear);ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                auto result=graphics.swap->Present(0,0);
                if(result==DXGI_ERROR_DEVICE_REMOVED||result==DXGI_ERROR_DEVICE_RESET)recreate();
                if(ui.commands.recreateGraphics){ui.commands.recreateGraphics=false;recreate();}
                if(ui.commands.resetWindow){ui.commands.resetWindow=false;centerWindow(window,windowContext.dpiScale);}
                if(ui.commands.minimize){ui.commands.minimize=false;ShowWindow(window,SW_MINIMIZE);}
                if(ui.commands.quit)break;
                nextFrame=std::max(nextFrame+std::chrono::microseconds(16667),std::chrono::steady_clock::now());
                auto delay=std::chrono::duration_cast<std::chrono::milliseconds>(nextFrame-std::chrono::steady_clock::now()).count();
                MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(std::max<std::int64_t>(1,delay)),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            }
            input.stop("Client shutdown.");ui.save();windowContext.input=nullptr;log.event("app","APP_STOPPED","Services and held inputs are shutting down.");
        }
        ImGui_ImplDX11_Shutdown();imguiLifetime.dx11=false;ImGui_ImplWin32_Shutdown();imguiLifetime.win32=false;ImGui::DestroyContext();graphics.clear();DestroyWindow(window);UnregisterClassW(wc.lpszClassName,instance);windowContext.profile=nullptr;
    }catch(const std::exception& e){MessageBoxW(nullptr,wide(e.what()).c_str(),L"Aster Client - startup or renderer error",MB_ICONERROR|MB_OK);exitCode=1;}
    if(SUCCEEDED(com))CoUninitialize();return exitCode;
}
