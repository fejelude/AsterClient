#pragma once
#include "auth/license.h"
#include "imgui.h"
#include <d3d11.h>
#include <future>
namespace aster {
struct UiCommands {bool quit=false,minimize=false,resetWindow=false,recreateGraphics=false;};
class Interface {
public:
    Interface(HWND,ID3D11Device*,Profile&,ConfigStore&,Diagnostics&,GameDetector&,InputService&,LicenseClient&,Hotkeys&);
    ~Interface();
    void render(float dpiScale);
    void acceptKey(const KeyPress&);
    bool capturingKey()const{return !captureId_.empty();}
    void changed();
    void save();
    void deviceChanged(ID3D11Device*);
    UiCommands commands;
    std::string message;
private:
    void background(ImDrawList*,ImVec2);
    void navigation(float,float);
    void modulePanel(std::string_view);
    void appearanceAccent();
    void appearanceColor();
    void appearanceImage();
    void hotkeyPanel();
    void plugs();
    void diagnosticPanel();
    void capturePopup();
    bool setting(const SettingDefinition&,ModuleConfig&);
    bool check(const char*,bool&,const char* help=nullptr);
    bool editString(const char*,std::string&,std::size_t size=2048);
    void help(const char*);
    void heading(const char*,const char* caption=nullptr);
    void bindButton(std::string_view);
    void applyProfile(Profile);
    void beginImageLoad(std::string source);
    void loadImageBytes(std::string_view);
    void pollTasks();
    std::filesystem::path chooseFile(bool save,const wchar_t* filter,const wchar_t* extension=nullptr);
    HWND window_;
    ID3D11Device* device_;
    ID3D11ShaderResourceView* image_=nullptr;
    int imageWidth_=0,imageHeight_=0;
    std::string imageBytes_,imageSource_;
    std::future<std::string> imageTask_;
    std::future<UpdateResult> updateTask_;
    std::future<std::filesystem::path> downloadTask_;
    std::optional<UpdateResult> update_;
    std::string updateMessage_,downloadMessage_;
    Profile& p_;
    ConfigStore& store_;
    Diagnostics& log_;
    GameDetector& detector_;
    InputService& input_;
    LicenseClient& license_;
    Hotkeys& hotkeys_;
    ImVec4 accent_{};
    float scale_=1.f;
    bool dirty_=false,captureRequested_=false,saveFailed_=false;
    double lastChange_=0,lastSave_=0;
    std::string captureId_,captureError_;
    std::optional<Keybind> captured_;
    int captureMovement_=-1;
    std::array<char,257> licenseKey_{};
    std::string reportPreview_;
    std::string lastMessage_;
    double messageChangedAt_=0;
    std::uint64_t profileGeneration_=0;
};
} // namespace aster
