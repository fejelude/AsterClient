#include "ui/interface.h"
#include "imgui_internal.h"
#include <commdlg.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace aster {
using Microsoft::WRL::ComPtr;
namespace {
ImVec4 rgba(const Color& c,float alpha=1.f){return {c[0],c[1],c[2],c[3]*alpha};}
ImU32 color(ImVec4 c,float a=1.f){c.w*=a;return ImGui::ColorConvertFloat4ToU32(c);}
ImVec4 readableOn(ImVec4 background,ImVec4 requested){
    auto linear=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
    auto luminance=[&](ImVec4 c){return .2126f*linear(c.x)+.7152f*linear(c.y)+.0722f*linear(c.z);};
    float a=luminance(background),b=luminance(requested);
    if((std::max(a,b)+.05f)/(std::min(a,b)+.05f)>=4.5f)return requested;
    return a>.179f?ImVec4{.015f,.015f,.025f,1}:ImVec4{1,1,1,1};
}
bool ready(std::future_status status){return status==std::future_status::ready;}
std::string uppercase(std::string value){for(auto& c:value)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return value;}
void icon(ImDrawList* draw,ImVec2 center,int index,ImU32 ink,float size){
    float r=size*.28f;
    if(index==0){draw->AddCircle(center,r,ink,32,1.8f);draw->AddCircle(center,r*.35f,ink,20,1.5f);
        draw->AddLine({center.x-r*1.4f,center.y},{center.x+r*1.4f,center.y},ink,1.7f);
        draw->AddLine({center.x,center.y-r*1.4f},{center.x,center.y+r*1.4f},ink,1.7f);}
    else if(index==1){
        draw->AddCircleFilled({center.x+r*.2f,center.y-r*1.1f},r*.24f,ink);
        draw->AddLine({center.x+r*.1f,center.y-r*.5f},{center.x-r*.15f,center.y+r*.35f},ink,2.4f);
        draw->AddPolyline(std::array<ImVec2,3>{{{center.x-r*.8f,center.y},{center.x-r*.4f,center.y-r*.4f},{center.x+r*.1f,center.y-r*.5f}}}.data(),3,ink,0,2.f);
        draw->AddLine({center.x+r*.05f,center.y-r*.4f},{center.x+r*.8f,center.y+r*.05f},ink,2.f);
        draw->AddPolyline(std::array<ImVec2,3>{{{center.x-r*.15f,center.y+r*.35f},{center.x+r*.55f,center.y+r*.7f},{center.x+r*.6f,center.y+r*1.25f}}}.data(),3,ink,0,2.2f);
        draw->AddLine({center.x-r*.15f,center.y+r*.35f},{center.x-r*.7f,center.y+r*1.25f},ink,2.2f);
    }else if(index==2){
        ImVec2 points[33];for(int i=0;i<33;++i){float x=(i/32.f*2-1)*r*1.5f;float y=std::sin(i/32.f*std::numbers::pi_v<float>)*r*.75f;points[i]={center.x+x,center.y-y};}
        draw->AddPolyline(points,33,ink,0,1.7f);for(auto& p:points)p.y=2*center.y-p.y;draw->AddPolyline(points,33,ink,0,1.7f);draw->AddCircleFilled(center,r*.4f,ink);
    }else for(int i=-1;i<=1;++i)draw->AddLine({center.x-r,center.y+i*r*.65f},{center.x+r,center.y+i*r*.65f},ink,2.f);
}
}
Interface::Interface(HWND window,ID3D11Device* device,Profile& profile,ConfigStore& store,Diagnostics& log,
    GameDetector& detector,InputService& input,LicenseClient& license,Hotkeys& hotkeys):window_(window),device_(device),
    p_(profile),store_(store),log_(log),detector_(detector),input_(input),license_(license),hotkeys_(hotkeys){
    input_.configure(p_);
    if(!p_.ui.imagePath.empty())try{imageBytes_=readFile(wide(p_.ui.imagePath),20*1024*1024);loadImageBytes(imageBytes_);}
    catch(...){message="Saved background is unavailable; the original procedural background is active.";}
}
Interface::~Interface(){if(image_)image_->Release();SecureZeroMemory(licenseKey_.data(),licenseKey_.size());}
void Interface::deviceChanged(ID3D11Device* device){
    if(image_){image_->Release();image_=nullptr;}device_=device;
    if(device_&&!imageBytes_.empty())try{loadImageBytes(imageBytes_);}catch(...){message="Background could not be restored after renderer reset.";}
}
void Interface::changed(){dirty_=true;lastChange_=ImGui::GetTime();input_.configure(p_);}
void Interface::save(){
    lastSave_=ImGui::GetTime();
    try{store_.save(p_);dirty_=false;saveFailed_=false;message="Configuration saved.";log_.event("config","CONFIG_SAVED","Atomic profile commit completed.");}
    catch(...){saveFailed_=true;message="Configuration could not be saved. The previous file is preserved.";log_.event("config","CONFIG_SAVE_FAILED",message);}
}
void Interface::help(const char* text){if(text&&ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)){ImGui::BeginTooltip();ImGui::PushTextWrapPos(340*scale_);ImGui::TextUnformatted(text);ImGui::PopTextWrapPos();ImGui::EndTooltip();}}
void Interface::heading(const char* label,const char* caption){
    ImGui::TextColored(accent_,"%s",label);if(caption){ImGui::PushStyleColor(ImGuiCol_Text,rgba(p_.ui.disabled));ImGui::TextWrapped("%s",caption);ImGui::PopStyleColor();}
    ImGui::Dummy({0,7*scale_});
}
bool Interface::check(const char* text,bool& value,const char* description){
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,30*scale_);bool changedValue=ImGui::Checkbox(text,&value);ImGui::PopStyleVar();help(description);return changedValue;
}
bool Interface::editString(const char* label,std::string& value,std::size_t size){
    std::vector<char> buffer(size+1,0);memcpy(buffer.data(),value.data(),std::min(size,value.size()));
    if(ImGui::InputText(label,buffer.data(),buffer.size())){value=buffer.data();return true;}return false;
}
void Interface::background(ImDrawList* draw,ImVec2 size){
    auto top=IM_COL32(7,9,18,255),bottom=IM_COL32(3,4,10,255);draw->AddRectFilledMultiColor({0,0},size,top,IM_COL32(12,8,24,255),bottom,bottom);
    float phase=static_cast<float>(ImGui::GetTime())*(p_.ui.animateBackground?p_.ui.backgroundSpeed*.12f:0.f);
    if(image_){
        float fit=p_.ui.scaleImage?std::max(size.x/imageWidth_,size.y/imageHeight_):1.f;
        float width=imageWidth_*fit*p_.ui.backgroundScale,height=imageHeight_*fit*p_.ui.backgroundScale;
        ImVec2 begin{(size.x-width)*.5f+p_.ui.backgroundX*scale_,(size.y-height)*.5f+p_.ui.backgroundY*scale_};
        auto tint=p_.ui.accentTint?accent_:ImVec4{1,1,1,1};tint.w=p_.ui.textureOpacity;
        draw->AddImage(reinterpret_cast<ImTextureID>(image_),begin,{begin.x+width,begin.y+height},{0,0},{1,1},color(tint));
    }else{
        // Original flowing contours; no reference artwork is embedded.
        for(int ribbon=0;ribbon<19;++ribbon){
            ImVec2 points[100];float band=ribbon/18.f;
            for(int i=0;i<100;++i){float t=i/99.f;float x=t*size.x;
                float wave=std::sin(t*7.6f+band*5+phase)*std::cos(t*3.3f-band*4+phase*.4f);
                float y=band*size.y+wave*size.y*.22f+std::sin(t*17+phase+band*8)*size.y*.06f;
                points[i]={x+(p_.ui.backgroundX*scale_*.2f),y+(p_.ui.backgroundY*scale_*.2f)};
            }
            ImVec4 ink=accent_;ink.w=p_.ui.textureOpacity*(.08f+.1f*std::sin(band*3.14f));
            draw->AddPolyline(points,100,color(ink),0,(8+10*std::sin(band*3.14f))*scale_*p_.ui.backgroundScale);
            ink.w*=2.1f;draw->AddPolyline(points,100,color(ink),0,1.1f*scale_);
        }
    }
    draw->AddRectFilled({0,0},{size.x,52*scale_},IM_COL32(6,7,13,235));
    if(p_.ui.huebar>0){
        for(int i=0;i<8;++i){float r,g,b;ImGui::ColorConvertHSVtoRGB(i/8.f,1,1,r,g,b);float r2,g2,b2;ImGui::ColorConvertHSVtoRGB((i+1)/8.f,1,1,r2,g2,b2);
            draw->AddRectFilledMultiColor({i*size.x/8,0},{(i+1)*size.x/8,p_.ui.huebarHeight*scale_},
                color({r,g,b,p_.ui.huebar}),color({r2,g2,b2,p_.ui.huebar}),color({r2,g2,b2,p_.ui.huebar}),color({r,g,b,p_.ui.huebar}));}
    }
}
void Interface::navigation(float y,float height){
    const char* labels[]={"COMBAT","MOVEMENT","VISUAL","SETTINGS"};
    auto draw=ImGui::GetWindowDrawList();float width=70*scale_,itemHeight=std::min(110*scale_,(height-20*scale_)/4);
    for(int i=0;i<4;++i){
        ImGui::SetCursorPos({10*scale_,y+i*(itemHeight+5*scale_)});auto begin=ImGui::GetCursorScreenPos();
        ImGui::PushID(i);bool chosen=p_.ui.category==i;
        ImGui::PushStyleColor(ImGuiCol_Button,chosen?ImVec4{accent_.x*.16f,accent_.y*.16f,accent_.z*.16f,.9f}:ImVec4{.025f,.03f,.05f,.78f});
        if(ImGui::Button("##navigation",{width,itemHeight})){p_.ui.category=i;changed();}
        bool hovered=ImGui::IsItemHovered();ImGui::PopStyleColor();help(labels[i]);
        if(chosen||hovered)draw->AddRect(begin,{begin.x+width,begin.y+itemHeight},color(accent_,chosen?.8f:.35f),p_.ui.rounding*scale_,0,1.f*scale_);
        icon(draw,{begin.x+width*.5f,begin.y+itemHeight*.48f},i,chosen?color(accent_):color(rgba(p_.ui.text)),32*scale_);
        if(chosen){draw->AddCircleFilled({begin.x+width*.5f,begin.y+itemHeight-12*scale_},2*scale_,color(accent_));}
        ImGui::PopID();
    }
}
bool Interface::setting(const SettingDefinition& def,ModuleConfig& module){
    bool changedValue=false;ImGui::PushID(def.id.c_str());
    if(!def.implemented)ImGui::BeginDisabled();
    if(def.kind==SettingKind::Boolean){bool flag=module.flag(def.id);changedValue=check(def.label.c_str(),flag,def.help.c_str());if(changedValue)module.flag(def.id,flag);}
    else{
        double value=module.number(def.id),step=def.step;
        if((def.id=="horizontal"||def.id=="vertical")&&module.values().contains("precision")&&!module.flag("precision"))step=1;
        float available=ImGui::GetContentRegionAvail().x;
        float labelWidth=std::min(160*scale_,available*.32f);
        float buttonsWidth=50*scale_;float sliderWidth=std::max(80*scale_,available-labelWidth-buttonsWidth-16*scale_);
        ImGui::SetNextItemWidth(sliderWidth);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{10*scale_,7*scale_});
        const auto sliderId=ImGui::GetID("##value");bool editing=ImGui::TempInputIsActive(sliderId);
        if(!editing){ImGui::PushStyleColor(ImGuiCol_FrameBg,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,{0,0,0,0});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_SliderGrab,{0,0,0,0});
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,{0,0,0,0});ImGui::PushStyleColor(ImGuiCol_Text,{0,0,0,0});}
        changedValue=ImGui::SliderScalar("##value",ImGuiDataType_Double,&value,&def.minimum,&def.maximum,def.format.c_str(),ImGuiSliderFlags_AlwaysClamp);
        if(!editing)ImGui::PopStyleColor(6);
        if(!ImGui::TempInputIsActive(sliderId)){
            auto begin=ImGui::GetItemRectMin(),end=ImGui::GetItemRectMax();auto draw=ImGui::GetWindowDrawList();
            float fraction=static_cast<float>((value-def.minimum)/(def.maximum-def.minimum));float alpha=def.implemented?1.f:.32f;
            draw->AddRectFilled(begin,end,color(rgba(p_.ui.secondary,.97f)),p_.ui.rounding*scale_);
            float fill=std::clamp(fraction*(end.x-begin.x),7*scale_,end.x-begin.x);
            draw->AddRectFilled(begin,{begin.x+fill,end.y},color(accent_,alpha),p_.ui.rounding*scale_);
            char label[80];snprintf(label,sizeof(label),def.format.c_str(),value);auto textSize=ImGui::CalcTextSize(label);
            auto ink=fraction>.58f?readableOn(accent_,rgba(p_.ui.activeText)):rgba(p_.ui.text);
            draw->AddText({begin.x+(end.x-begin.x-textSize.x)*.5f,begin.y+(end.y-begin.y-textSize.y)*.5f},color(ink,alpha),label);
            if(ImGui::IsItemFocused()||ImGui::IsItemHovered())draw->AddRect(begin,end,color(accent_,.7f*alpha),p_.ui.rounding*scale_,0,1.f);
        }
        ImGui::PopStyleVar();help(def.help.c_str());ImGui::SameLine();
        if(ImGui::Button("-",{22*scale_,30*scale_})){value-=step;changedValue=true;}help("Decrease by one step.");ImGui::SameLine(0,3*scale_);
        if(ImGui::Button("+",{22*scale_,30*scale_})){value+=step;changedValue=true;}help("Increase by one step.");ImGui::SameLine();
        ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(def.label.c_str());
        if(changedValue){value=std::clamp(std::round((value-def.minimum)/step)*step+def.minimum,def.minimum,def.maximum);module.number(def.id,value);}
    }
    if(!def.implemented){ImGui::EndDisabled();help(def.help.c_str());}
    ImGui::PopID();return changedValue;
}
void Interface::bindButton(std::string_view id){
    auto key=keyName(p_.bindings.at(std::string(id)));auto text="BOUND: "+key;
    if(ImGui::Button(text.c_str())){captureId_=id;captureMovement_=-1;captureRequested_=true;captured_.reset();captureError_.clear();input_.stop("Key binding capture is open.");}
    help("Capture a new optional key binding. Escape cancels; Delete unbinds. Conflicting bindings are never toggled together.");
}
void Interface::modulePanel(std::string_view id){
    const auto& def=definition(id);auto& module=p_.modules.at(std::string(id));auto game=detector_.snapshot();auto runtime=input_.status();
    auto health=computeHealth(def,module,game,runtime);
    heading(uppercase(def.name).c_str(),def.description.c_str());
    if(def.capability==Capability::Engine){
        ImGui::PushStyleColor(ImGuiCol_Text,{1.f,.64f,.32f,1});ImGui::TextWrapped("UNSUPPORTED  /  No verified game-engine adapter%s%s.",game.build.empty()?"":" for ",game.build.c_str());ImGui::PopStyleColor();
        ImGui::TextDisabled("Settings below are saved configuration; they do not alter Minecraft.");
    }else{
        ImGui::TextColored({1.f,.72f,.3f,1},"INPUT CAPABILITY  /  In-world effect unverified");
        ImGui::TextWrapped("%s",health.detail.c_str());
    }
    ImGui::Dummy({0,8*scale_});
    if(check("Enable request",module.enabled,"This saves your requested state. The adjacent runtime status reports whether the capability can actually run.")){changed();}
    ImGui::SameLine();ImGui::TextColored(health.state==ModuleState::Failed?ImVec4{1,.3f,.3f,1}:accent_,"%s",stateName(health.state).data());
    ImGui::SameLine();bindButton(id);ImGui::Separator();ImGui::Dummy({0,8*scale_});
    if(id=="aim"){
        bool closet=module.selectedProfile=="closet";
        if(ImGui::RadioButton("CLOSET",closet)){module.selectedProfile="closet";changed();}ImGui::SameLine();
        if(ImGui::RadioButton("BLATANT",!closet)){module.selectedProfile="blatant";changed();}help("Each named profile stores independent values; runtime semantics remain unverified.");ImGui::Dummy({0,6*scale_});
    }
    if(id=="velocity"){
        float points[81];const double xz=module.number("horizontal")/100.,vertical=module.number("vertical")/100.;
        for(int i=0;i<81;++i){float t=i/80.f;points[i]=static_cast<float>(4*t*(1-t)*vertical);}
        ImGui::PushStyleColor(ImGuiCol_PlotLines,accent_);ImGui::PlotLines("##preview",points,81,0,"CONFIGURATION PREVIEW / not measured displacement",0,1,{ImGui::GetContentRegionAvail().x,90*scale_});ImGui::PopStyleColor();
        if(module.flag("percentage"))ImGui::Text("X/Z factor %.4f  /  Y factor %.4f",xz,vertical);
        else ImGui::Text("X/Z %.2f  /  Y %.2f",module.number("horizontal"),module.number("vertical"));
    }
    if(id=="timer"){
        auto multiplier=module.number("multiplier");ImGui::TextColored(accent_,"REQUESTED TPS  %.2f   /   SPEED BOOST  %.2f%%",timerTps(multiplier),timerBoost(multiplier));
        ImGui::TextDisabled("Calculated target; actual game tick rate is not measured.");ImGui::Dummy({0,8*scale_});
    }
    if(id=="reach"){
        auto distance=module.number("distance");ImGui::Text("%.1f%% of 3.00 m baseline   /   +%.2f block(s)",distance/3.*100.,distance-3.);ImGui::Dummy({0,6*scale_});
    }
    for(const auto& settingDef:def.settings)if(setting(settingDef,module))changed();
    if(id=="clicker"){
        ImGui::Dummy({0,8*scale_});ImGui::Separator();
        ImGui::Text("Requests       L %llu   R %llu",runtime.requested[0],runtime.requested[1]);
        ImGui::Text("Windows sent   L %llu   R %llu",runtime.sent[0],runtime.sent[1]);
        ImGui::TextDisabled("Minecraft acknowledgments: unavailable");
        ImGui::TextWrapped("F8 arms input in a private world. F9 stops. Release and re-press a held mouse button after arming. Chat/menu keys and focus loss pause automation.");
    }
    if(id=="auto360"){ImGui::TextDisabled("Measured rotations: unavailable (no orientation telemetry).");}
    if(id=="zoom"){ImGui::TextDisabled("Game FOV modifier: unavailable (no camera adapter).");}
}
void Interface::appearanceAccent(){
    heading("ACCENT","One accent ties the navigation, sliders and status outlines together.");
    bool update=false;
    update|=check("Animate hue",p_.ui.animateHue,"Rotate the accent hue over time. Disable to use the selected fixed color.");
    ImGui::SetNextItemWidth(-110*scale_);update|=ImGui::SliderFloat("Hue Speed",&p_.ui.hueSpeed,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-110*scale_);update|=ImGui::SliderFloat("Rounded",&p_.ui.rounding,0,24,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-110*scale_);update|=ImGui::SliderFloat("UI Scale",&p_.ui.scale,.8f,1.6f,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
    help("Scale all controls and text. Scrollable panels keep controls reachable at larger sizes.");
    ImGui::TextDisabled("%.2f ms / frame  |  %.1f FPS",1000.f/std::max(1.f,ImGui::GetIO().Framerate),ImGui::GetIO().Framerate);
    ImGui::Dummy({0,8*scale_});
    ImGui::SetNextItemWidth(std::min(350*scale_,ImGui::GetContentRegionAvail().x));
    update|=ImGui::ColorPicker3("##accent-picker",p_.ui.accent.data(),ImGuiColorEditFlags_PickerHueWheel|ImGuiColorEditFlags_DisplayRGB|ImGuiColorEditFlags_DisplayHex);
    if(update)changed();
}
void Interface::appearanceColor(){
    heading("GUI COLORS","RGB and hex edits share the same stored values.");bool update=false;
    struct Entry{const char* label;Color* color;};
    for(auto entry:{Entry{"Borders",&p_.ui.border},Entry{"Primary",&p_.ui.primary},Entry{"Secondary",&p_.ui.secondary},
        Entry{"Text Primary",&p_.ui.text},Entry{"Text Disabled",&p_.ui.disabled},Entry{"Text Active",&p_.ui.activeText}}){
        ImGui::SetNextItemWidth(-135*scale_);update|=ImGui::ColorEdit3(entry.label,entry.color->data(),ImGuiColorEditFlags_DisplayRGB|ImGuiColorEditFlags_Uint8);
    }
    ImGui::Dummy({0,10*scale_});ImGui::SetNextItemWidth(-135*scale_);update|=ImGui::SliderFloat("Huebar",&p_.ui.huebar,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-135*scale_);update|=ImGui::SliderFloat("Height",&p_.ui.huebarHeight,1,8,"%.0f px",ImGuiSliderFlags_AlwaysClamp);
    update|=check("Footer",p_.ui.footer,"Show connection and input-session status along the bottom edge.");
    update|=check("DPI Awareness (restart)",p_.ui.dpiAware,"Applies when the next native window is created. Windows may rescale the window if disabled.");
    if(update)changed();
}
std::filesystem::path Interface::chooseFile(bool saveFile,const wchar_t* filter,const wchar_t* extension){
    wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window_;
    dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrFilter=filter;dialog.lpstrDefExt=extension;
    dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(saveFile?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    if(saveFile?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog))return path;return {};
}
void Interface::beginImageLoad(std::string source){
    if(imageTask_.valid())return;
    imageTask_=std::async(std::launch::async,[source=std::move(source)]{
        if(source.rfind("https://",0)==0){auto response=httpRequest(source,"GET",{},20*1024*1024);
            if(response.status!=200)throw std::runtime_error("Image download failed.");return response.body;}
        if(source.find("://")!=std::string::npos)throw std::runtime_error("Only HTTPS image URLs or local files are supported.");
        return readFile(wide(source),20*1024*1024);
    });message="Loading background image...";
}
void Interface::loadImageBytes(std::string_view bytes){
    if(!device_||bytes.empty()||bytes.size()>20*1024*1024)throw std::runtime_error("Invalid image size.");
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))throw std::runtime_error("Windows image decoder unavailable.");
    ComPtr<IWICStream> stream;factory->CreateStream(&stream);
    if(!stream||FAILED(stream->InitializeFromMemory(reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())),static_cast<DWORD>(bytes.size()))))throw std::runtime_error("Image stream failed.");
    ComPtr<IWICBitmapDecoder> decoder;if(FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder)))throw std::runtime_error("Not a supported PNG, JPEG, GIF or BMP image.");
    ComPtr<IWICBitmapFrameDecode> frame;if(FAILED(decoder->GetFrame(0,&frame)))throw std::runtime_error("Image has no frame.");
    UINT width=0,height=0;frame->GetSize(&width,&height);
    if(!width||!height||width>4096||height>4096)throw std::runtime_error("Image dimensions must be at most 4096 x 4096.");
    ComPtr<IWICFormatConverter> converter;factory->CreateFormatConverter(&converter);
    if(!converter||FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))throw std::runtime_error("Image conversion failed.");
    std::vector<BYTE> pixels(static_cast<std::size_t>(width)*height*4);
    if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels.size()),pixels.data())))throw std::runtime_error("Image decode failed.");
    D3D11_TEXTURE2D_DESC description{};description.Width=width;description.Height=height;description.MipLevels=description.ArraySize=1;
    description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};ComPtr<ID3D11Texture2D> texture;
    if(FAILED(device_->CreateTexture2D(&description,&data,&texture)))throw std::runtime_error("Graphics texture allocation failed.");
    ID3D11ShaderResourceView* view=nullptr;
    if(FAILED(device_->CreateShaderResourceView(texture.Get(),nullptr,&view)))throw std::runtime_error("Graphics texture view failed.");
    if(image_)image_->Release();image_=view;imageWidth_=static_cast<int>(width);imageHeight_=static_cast<int>(height);
}
void Interface::appearanceImage(){
    heading("BACKGROUND","Original flowing contours, or an image you choose. The first frame of animated files is used.");bool update=false;
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("Panel Opacity",&p_.ui.panelOpacity,.1f,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    help("Opacity of the control surface over the background.");
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("Texture Opacity",&p_.ui.textureOpacity,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("Scaling",&p_.ui.backgroundScale,.1f,5,"%.2fx",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("Speed",&p_.ui.backgroundSpeed,0,4,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    help("Animation speed of the original flowing contours. Imported images remain still.");
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("X Position",&p_.ui.backgroundX,-2000,2000,"%.0f px",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(-145*scale_);update|=ImGui::SliderFloat("Y Position",&p_.ui.backgroundY,-2000,2000,"%.0f px",ImGuiSliderFlags_AlwaysClamp);
    ImGui::Dummy({0,5*scale_});
    if(ImGui::Button("Choose Image")){auto file=chooseFile(false,L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0All files\0*.*\0\0");if(!file.empty())beginImageLoad(utf8(file.wstring()));}
    ImGui::SameLine();if(ImGui::Button("Use Contours")){if(image_){image_->Release();image_=nullptr;}imageBytes_.clear();p_.ui.imagePath.clear();changed();}
    ImGui::SetNextItemWidth(-105*scale_);editString("##image-source",imageSource_);help("Local image path or HTTPS URL. Network access occurs only when Load Image is pressed.");ImGui::SameLine();
    ImGui::BeginDisabled(imageTask_.valid()||imageSource_.empty());if(ImGui::Button("Load Image"))beginImageLoad(imageSource_);ImGui::EndDisabled();
    update|=check("Animate Background",p_.ui.animateBackground);ImGui::SameLine();update|=check("Scale Image",p_.ui.scaleImage,"Fit the image to cover the control window before applying Scaling.");
    update|=check("Accent Tint",p_.ui.accentTint);ImGui::SameLine();update|=check("Lock Window",p_.ui.windowLocked,"Prevent window dragging and border resizing. This does not stop configuration edits.");
    if(ImGui::Button("Reset Window"))commands.resetWindow=true;ImGui::SameLine();
    if(ImGui::Button("Reset Colors")){
        auto defaults=defaultProfile().ui;p_.ui.accent=defaults.accent;p_.ui.border=defaults.border;p_.ui.primary=defaults.primary;p_.ui.secondary=defaults.secondary;
        p_.ui.text=defaults.text;p_.ui.disabled=defaults.disabled;p_.ui.activeText=defaults.activeText;update=true;
    }
    ImGui::BeginDisabled();bool unused=false;check("Anti OBS / unsupported",unused,"Reference-only control. This application does not hide itself from capture or monitoring tools.");ImGui::EndDisabled();
    if(update)changed();
}
void Interface::acceptKey(const KeyPress& press){
    if(captureId_.empty()||(captureMovement_<0&&isModifierKey(press.key)))return;
    if(press.key==VK_ESCAPE){captureId_.clear();captured_.reset();return;}
    if(press.key==VK_DELETE||press.key==VK_BACK){captured_=Keybind{};return;}
    Keybind key{press.key,press.modifiers};if(captureMovement_>=0){key.modifiers=0;
        if(key.key==VK_LCONTROL||key.key==VK_RCONTROL)key.key=VK_CONTROL;
        if(key.key==VK_LSHIFT||key.key==VK_RSHIFT)key.key=VK_SHIFT;
        if(key.key==VK_LMENU||key.key==VK_RMENU)key.key=VK_MENU;}
    if(captureMovement_>=0&&(key.key==VK_F8||key.key==VK_F9)){captureError_="F8 and F9 are reserved for input-session control.";return;}
    if(captureMovement_<0&&!isSafeBinding(key,captureId_=="hide")){captureError_="Choose a non-reserved key. Global Hide needs a function key or Ctrl/Alt combination.";return;}
    captured_=key;captureError_.clear();
}
void Interface::capturePopup(){
    if(captureRequested_){ImGui::OpenPopup("Capture binding");captureRequested_=false;}
    ImGui::SetNextWindowSize({420*scale_,0},ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("Capture binding",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        if(captureId_.empty()){ImGui::CloseCurrentPopup();ImGui::EndPopup();return;}
        ImGui::Text("Binding: %s",captureId_.c_str());ImGui::TextWrapped("Press a key. Escape cancels; Delete unbinds. F8 arms and F9 stops input.");
        // ImGui keyboard state also supports UI tests generated through Windows input.
        for(int key=ImGuiKey_NamedKey_BEGIN;key<ImGuiKey_NamedKey_END;++key)if(ImGui::IsKeyPressed(static_cast<ImGuiKey>(key),false)){
            auto k=static_cast<ImGuiKey>(key);int vk=0;
            if(k>=ImGuiKey_A&&k<=ImGuiKey_Z)vk='A'+(k-ImGuiKey_A);
            else if(k>=ImGuiKey_0&&k<=ImGuiKey_9)vk='0'+(k-ImGuiKey_0);
            else if(k>=ImGuiKey_F1&&k<=ImGuiKey_F24)vk=VK_F1+(k-ImGuiKey_F1);
            else if(k==ImGuiKey_Tab)vk=VK_TAB;else if(k==ImGuiKey_Space)vk=VK_SPACE;
            else if(k==ImGuiKey_Escape)vk=VK_ESCAPE;else if(k==ImGuiKey_Delete)vk=VK_DELETE;
            else if(k==ImGuiKey_LeftArrow)vk=VK_LEFT;else if(k==ImGuiKey_RightArrow)vk=VK_RIGHT;
            else if(k==ImGuiKey_UpArrow)vk=VK_UP;else if(k==ImGuiKey_DownArrow)vk=VK_DOWN;
            else if(captureMovement_>=0&&(k==ImGuiKey_LeftCtrl||k==ImGuiKey_RightCtrl))vk=VK_CONTROL;
            else if(captureMovement_>=0&&(k==ImGuiKey_LeftShift||k==ImGuiKey_RightShift))vk=VK_SHIFT;
            else if(captureMovement_>=0&&(k==ImGuiKey_LeftAlt||k==ImGuiKey_RightAlt))vk=VK_MENU;
            if(vk){auto& io=ImGui::GetIO();acceptKey({vk,(io.KeyAlt?1u:0u)|(io.KeyCtrl?2u:0u)|(io.KeyShift?4u:0u),0});}
        }
        if(captured_){ImGui::TextColored(accent_,"%s",keyName(*captured_).c_str());
            if(captureMovement_<0&&captured_->key){for(const auto& [id,key]:p_.bindings)if(id!=captureId_&&key==*captured_)ImGui::TextColored({1,.65f,.3f,1},"Conflicts with %s",id.c_str());}
        }else ImGui::TextColored(accent_,"Listening...");
        if(!captureError_.empty())ImGui::TextWrapped("%s",captureError_.c_str());
        ImGui::BeginDisabled(!captured_.has_value());
        if(ImGui::Button("Save Binding")){
            auto old=p_.bindings.at(captureMovement_<0?captureId_:"hide");bool ok=true;
            if(captureMovement_>=0){if(captured_->key)p_.movement[captureMovement_]=captured_->key;else captureError_="Movement controls require a key.";ok=captured_->key!=0;}
            else if(captureId_=="hide"&&!hotkeys_.configureHide(*captured_)){hotkeys_.configureHide(old);captureError_="Windows rejected this hotkey; another app may own it.";ok=false;}
            else p_.bindings[captureId_]=*captured_;
            if(ok){changed();save();captureId_.clear();captured_.reset();ImGui::CloseCurrentPopup();}
        }
        ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel")){captureId_.clear();captured_.reset();ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
}
void Interface::hotkeyPanel(){
    heading("KEYBINDS","Module toggles work only while Minecraft is foreground. Hide is global. F8/F9 are reserved.");
    if(ImGui::BeginTable("bindings",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)){
        ImGui::TableSetupColumn("Request",ImGuiTableColumnFlags_WidthFixed,70*scale_);ImGui::TableSetupColumn("Module");ImGui::TableSetupColumn("Binding");ImGui::TableHeadersRow();
        std::vector<std::string> ids{"hide","reach","velocity","timer","hitbox","aim","clicker","sprint","zoom","auto360","stap","walk"};
        for(const auto& id:ids){ImGui::PushID(id.c_str());ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);
            if(id!="hide"){auto& module=p_.modules.at(id);if(check("##enabled",module.enabled))changed();}else ImGui::TextDisabled("GLOBAL");
            ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(id=="hide"?"Hide / Restore":definition(id).name.c_str());
            if(id!="hide"&&definition(id).capability==Capability::Engine)help("No verified engine adapter. A requested state is saved, but the game is unchanged.");
            ImGui::TableSetColumnIndex(2);bindButton(id);ImGui::SameLine();
            if(ImGui::SmallButton("x")){if(id!="hide"||hotkeys_.configureHide({})){p_.bindings[id]={};changed();save();}}help("Unbind this key. GUI operation remains available.");ImGui::PopID();
        }ImGui::EndTable();
    }
    for(const auto& conflict:bindingConflicts(p_))ImGui::TextColored({1,.65f,.3f,1},"%s",conflict.c_str());
    ImGui::Dummy({0,15*scale_});heading("MINECRAFT CONTROLS","Match your own Minecraft movement bindings. Duplicate controls are allowed.");
    const char* names[]={"Forward","Left","Backward","Right","Sprint","Crouch"};
    for(int i=0;i<6;++i){ImGui::PushID(i);ImGui::Text("%-10s",names[i]);ImGui::SameLine(150*scale_);
        if(ImGui::Button(keyName({p_.movement[i],0}).c_str(),{130*scale_,0})){captureId_=names[i];captureMovement_=i;captureRequested_=true;captured_.reset();captureError_.clear();input_.stop("Movement key capture is open.");}ImGui::PopID();}
    if(ImGui::Button("Reset Movement Map")){p_.movement=defaultProfile().movement;changed();save();}
    ImGui::TextWrapped("Sprint holds the configured sprint key while forward is held. Auto Walk holds Forward. Auto S-Tap remains unsupported because its timing is undocumented.");
}
void Interface::applyProfile(Profile profile){
    validate(profile);input_.stop("Profile changed. Press F8 in-world to resume.");p_=std::move(profile);++profileGeneration_;
    if(!hotkeys_.configureHide(p_.bindings.at("hide")))message="Hide hotkey conflicts with another app. The tray icon can restore the window.";
    license_.configure(p_.license);input_.configure(p_);changed();save();
    deviceChanged(device_);
}
void Interface::plugs(){
    heading("ASTER CLIENT","An original native client lab, guided by your clean-room specification.");
    if(ImGui::Button("SAVE CONFIG",{150*scale_,0}))save();ImGui::SameLine();ImGui::TextDisabled(dirty_?"Unsaved changes":"Saved");
    if(ImGui::Button("Export Profile")){
        auto file=chooseFile(true,L"JSON profiles\0*.json\0\0",L"json");
        if(!file.empty())try{atomicWrite(file,profileToJson(p_).dump(2)+"\n");message="Profile exported; no credentials are included.";}catch(...){message="Profile export failed; existing file preserved.";}
    }
    ImGui::SameLine();if(ImGui::Button("Import Profile")){
        auto file=chooseFile(false,L"JSON profiles\0*.json\0\0");
        if(!file.empty())try{std::vector<std::string> warnings;auto imported=profileFromJson(Json::parse(readFile(file)),warnings);store_.save(p_);applyProfile(std::move(imported));message="Profile imported. Input remains paused.";}
        catch(...){message="Profile import failed; current configuration preserved.";}
    }
    auto game=detector_.snapshot();ImGui::Dummy({0,8*scale_});ImGui::Text("Minecraft  %s",game.build.empty()?"not detected":game.build.c_str());
    ImGui::Text("Client     %s / Windows x64 / DirectX 11",kVersion);ImGui::TextDisabled("Engine adapters: none verified. Input bridge: experimental.");
    ImGui::Separator();ImGui::Dummy({0,8*scale_});
    auto license=license_.snapshot();heading("OWN LICENSE SERVICE",license.detail.c_str());
    ImGui::TextColored(accent_,"%s",licenseStateName(license.state).data());
    if(!license.entitlement.empty())ImGui::Text("Entitlement: %s",license.entitlement.c_str());
    if(license.offlineUntil)ImGui::Text("Offline grace remaining: %.1f hours",std::max<std::int64_t>(0,license.offlineUntil-unixNow())/3600.);
    bool edited=false;ImGui::SetNextItemWidth(-95*scale_);edited|=editString("Service URL",p_.license.endpoint);
    help("Use an HTTPS endpoint operated by you. Explicit development fixtures may use http://127.0.0.1 only.");
    ImGui::SetNextItemWidth(-95*scale_);edited|=editString("Public Key",p_.license.publicKeyPath);
    help("Path to your service's RSA public PEM. The private signing key stays on the service.");
    if(ImGui::Button("Choose Service Public Key")){auto file=chooseFile(false,L"Public PEM key\0*.pem\0All files\0*.*\0\0");if(!file.empty()){p_.license.publicKeyPath=utf8(file.wstring());edited=true;}}
    edited|=check("Require Own License",p_.license.required,"An owner-operated entitlement can gate experimental input. The default local developer build does not require a purchased key.");
    if(ImGui::Button("Apply Service Settings")){license_.configure(p_.license);input_.stop("License service settings changed.");save();}
    ImGui::SetNextItemWidth(-105*scale_);ImGui::InputText("##license-key",licenseKey_.data(),licenseKey_.size(),ImGuiInputTextFlags_Password);ImGui::SameLine();
    ImGui::BeginDisabled(license.busy||p_.license.endpoint.empty()||p_.license.publicKeyPath.empty());
    if(ImGui::Button("Activate")){
        if(license_.activate(licenseKey_.data())){SecureZeroMemory(licenseKey_.data(),licenseKey_.size());message="Activation started against your configured service.";}
    }ImGui::EndDisabled();
    ImGui::BeginDisabled(license.busy||p_.license.endpoint.empty());
    if(ImGui::Button("Refresh Session"))license_.refresh();ImGui::SameLine();if(ImGui::Button("Forget Local Session")){input_.stop("Local license session removed.");license_.forget();}ImGui::EndDisabled();
    ImGui::Dummy({0,12*scale_});ImGui::Separator();heading("RELEASE UPDATES","Update feeds require your signing key; packages also require a trusted, pinned Authenticode publisher.");
    ImGui::SetNextItemWidth(-95*scale_);edited|=editString("Feed URL",p_.updates.feedUrl);
    ImGui::SetNextItemWidth(-95*scale_);edited|=editString("Feed Key",p_.updates.publicKeyPath);
    ImGui::SetNextItemWidth(-95*scale_);edited|=editString("Signer SHA256",p_.updates.signerThumbprint,64);
    const char* channels[]={"stable","preview"};int selected=p_.updates.channel=="preview"?1:0;
    if(ImGui::Combo("Channel",&selected,channels,2)){p_.updates.channel=channels[selected];edited=true;update_.reset();}
    ImGui::BeginDisabled(updateTask_.valid()||p_.updates.feedUrl.empty()||p_.updates.publicKeyPath.empty());
    if(ImGui::Button("Check Signed Feed")){
        update_.reset();updateMessage_="Checking signed release feed...";
        updateTask_=std::async(std::launch::async,[config=p_.updates]{return checkUpdate(config);});
    }ImGui::EndDisabled();
    if(!updateMessage_.empty())ImGui::TextWrapped("%s",updateMessage_.c_str());
    if(update_&&update_->available){
        ImGui::Text("Available: %s",update_->version.c_str());ImGui::TextWrapped("%s",update_->notes.c_str());
        ImGui::BeginDisabled(downloadTask_.valid());if(ImGui::Button("Download and Verify")){
            downloadMessage_="Downloading and verifying release...";auto directory=store_.path().parent_path().parent_path()/L"updates";
            downloadTask_=std::async(std::launch::async,[update=*update_,directory]{return downloadUpdate(update,directory);});
        }ImGui::EndDisabled();
    }
    if(!downloadMessage_.empty())ImGui::TextWrapped("%s",downloadMessage_.c_str());
    ImGui::Dummy({0,10*scale_});ImGui::BeginDisabled();ImGui::Button("Discord / unavailable");ImGui::SameLine();ImGui::Button("Vouch / unavailable");ImGui::EndDisabled();
    help("No community or review destination was supplied for this independent application.");
    if(edited)changed();
}
void Interface::diagnosticPanel(){
    auto game=detector_.snapshot();auto runtime=input_.status();
    heading("DIAGNOSTICS","No telemetry leaves this app automatically. Preview exactly what Copy Report shares to your clipboard.");
    ImGui::Text("Game identity  %s",game.identityVerified?"verified Store package":"not verified");
    ImGui::Text("Build          %s",game.build.empty()?"not available":game.build.c_str());
    ImGui::Text("Foreground     %s",game.foreground?"yes":"no");ImGui::Text("Cursor captured %s",game.cursorCaptured?"yes":"no");
    ImGui::Text("Input session  %s",runtime.armed?"armed / unverified in game":"paused");ImGui::TextWrapped("%s",runtime.stopReason.c_str());
    ImGui::Text("Send failures  %llu",runtime.failures);ImGui::Text("Swapped mouse buttons  %s",GetSystemMetrics(SM_SWAPBUTTON)?"yes (logical streams)":"no");
    ImGui::Text("F9 emergency hotkey  %s",hotkeys_.panicRegistered()?"registered":"unavailable; local F9 and Stop remain available");
    if(ImGui::Button("STOP ALL INPUT")){input_.stop("Stopped from diagnostics.");message="Input stopped.";}
    ImGui::SameLine();if(ImGui::Button("Recreate Renderer"))commands.recreateGraphics=true;
    if(ImGui::Button("Refresh Report Preview"))reportPreview_=log_.report(p_,game,runtime);ImGui::SameLine();
    ImGui::BeginDisabled(reportPreview_.empty());if(ImGui::Button("Copy Diagnostic Report"))try{copyText(window_,reportPreview_);message="The previewed diagnostic report was copied.";}catch(...){message="Clipboard is busy; report was not copied.";}ImGui::EndDisabled();
    if(!reportPreview_.empty()){
        ImGui::SetNextItemWidth(-1);ImGui::InputTextMultiline("##report",reportPreview_.data(),reportPreview_.size()+1,{ImGui::GetContentRegionAvail().x,220*scale_},ImGuiInputTextFlags_ReadOnly);
    }
    ImGui::Dummy({0,10*scale_});heading("CAPABILITY MATRIX");
    if(ImGui::BeginTable("health",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("Module");ImGui::TableSetupColumn("Runtime state");ImGui::TableHeadersRow();
        for(const auto& d:moduleDefinitions()){
            auto h=computeHealth(d,p_.modules.at(d.id),game,runtime);ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::TextUnformatted(d.name.c_str());
            ImGui::TableSetColumnIndex(1);ImGui::TextUnformatted(stateName(h.state).data());help(h.detail.c_str());
        }ImGui::EndTable();
    }
}
void Interface::pollTasks(){
    if(imageTask_.valid()&&ready(imageTask_.wait_for(std::chrono::seconds(0)))){
        try{
            auto bytes=imageTask_.get();loadImageBytes(bytes);auto path=store_.path().parent_path().parent_path()/L"assets"/L"background.image";
            atomicWrite(path,bytes,false);imageBytes_=std::move(bytes);p_.ui.imagePath=utf8(path.wstring());changed();save();message="Background image loaded and stored locally.";
        }catch(const std::exception& e){message=std::string("Image not loaded: ")+e.what();}
    }
    if(updateTask_.valid()&&ready(updateTask_.wait_for(std::chrono::seconds(0)))){
        try{update_=updateTask_.get();updateMessage_=update_->available?"Release metadata signature verified.":"No newer release is listed in this signed feed.";}
        catch(const std::exception& e){updateMessage_=std::string("Update check failed: ")+e.what();}
    }
    if(downloadTask_.valid()&&ready(downloadTask_.wait_for(std::chrono::seconds(0)))){
        try{auto path=downloadTask_.get();downloadMessage_="Download verified and saved: "+utf8(path.wstring())+". Close this app before replacing it; the prior executable is preserved.";}
        catch(const std::exception& e){downloadMessage_=std::string("Release was not accepted: ")+e.what();}
    }
}
void Interface::render(float dpiScale){
    scale_=p_.ui.scale*dpiScale;ImGui::GetIO().FontGlobalScale=scale_*.5f;
    float hue,saturation,value;ImGui::ColorConvertRGBtoHSV(p_.ui.accent[0],p_.ui.accent[1],p_.ui.accent[2],hue,saturation,value);
    if(p_.ui.animateHue&&p_.ui.hueSpeed>0)hue=std::fmod(hue+static_cast<float>(ImGui::GetTime())*p_.ui.hueSpeed*.015f,1.f);
    ImGui::ColorConvertHSVtoRGB(hue,saturation,value,accent_.x,accent_.y,accent_.z);accent_.w=1;
    ImGuiStyle style;style.WindowPadding={14,14};style.FramePadding={9,5};style.ItemSpacing={8,8};style.ItemInnerSpacing={7,5};
    style.WindowRounding=p_.ui.rounding;style.ChildRounding=p_.ui.rounding;style.FrameRounding=p_.ui.rounding;style.GrabRounding=p_.ui.rounding;
    style.ScrollbarRounding=p_.ui.rounding;style.PopupRounding=p_.ui.rounding;style.WindowBorderSize=0;style.ChildBorderSize=1;style.FrameBorderSize=0;
    style.ScrollbarSize=7;style.GrabMinSize=12;style.ScaleAllSizes(scale_);
    style.Colors[ImGuiCol_Text]=rgba(p_.ui.text);style.Colors[ImGuiCol_TextDisabled]=rgba(p_.ui.disabled);
    style.Colors[ImGuiCol_WindowBg]={.025f,.03f,.05f,1};style.Colors[ImGuiCol_ChildBg]=rgba(p_.ui.primary,p_.ui.panelOpacity);
    style.Colors[ImGuiCol_PopupBg]={.045f,.05f,.075f,.99f};style.Colors[ImGuiCol_Border]=rgba(p_.ui.border);
    style.Colors[ImGuiCol_FrameBg]=rgba(p_.ui.secondary,.95f);style.Colors[ImGuiCol_FrameBgHovered]={accent_.x*.25f,accent_.y*.25f,accent_.z*.25f,.95f};
    style.Colors[ImGuiCol_FrameBgActive]={accent_.x*.35f,accent_.y*.35f,accent_.z*.35f,1};
    style.Colors[ImGuiCol_Button]=rgba(p_.ui.secondary,.95f);style.Colors[ImGuiCol_ButtonHovered]={accent_.x*.3f,accent_.y*.3f,accent_.z*.3f,1};
    style.Colors[ImGuiCol_ButtonActive]=accent_;style.Colors[ImGuiCol_CheckMark]=accent_;style.Colors[ImGuiCol_SliderGrab]=accent_;
    style.Colors[ImGuiCol_SliderGrabActive]=accent_;style.Colors[ImGuiCol_Header]={accent_.x*.2f,accent_.y*.2f,accent_.z*.2f,1};
    style.Colors[ImGuiCol_HeaderHovered]={accent_.x*.4f,accent_.y*.4f,accent_.z*.4f,1};style.Colors[ImGuiCol_HeaderActive]=accent_;
    style.Colors[ImGuiCol_ScrollbarBg]={0,0,0,.1f};style.Colors[ImGuiCol_ScrollbarGrab]={accent_.x,accent_.y,accent_.z,.45f};
    style.Colors[ImGuiCol_NavHighlight]=accent_;style.Colors[ImGuiCol_TableHeaderBg]={.07f,.08f,.12f,.9f};
    style.Colors[ImGuiCol_TableRowBgAlt]={.2f,.22f,.3f,.08f};style.Colors[ImGuiCol_Separator]=rgba(p_.ui.border,.8f);ImGui::GetStyle()=style;
    pollTasks();if(dirty_&&ImGui::GetTime()-lastChange_>.8&&ImGui::GetTime()-lastSave_>(saveFailed_?10.:1.))save();
    if(message!=lastMessage_){lastMessage_=message;messageChangedAt_=ImGui::GetTime();}
    auto size=ImGui::GetIO().DisplaySize;ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::Begin("AsterRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();auto draw=ImGui::GetWindowDrawList();background(draw,size);
    ImGui::SetCursorPos({20*scale_,16*scale_});ImGui::TextColored(accent_,"A S T E R");ImGui::SameLine();ImGui::TextDisabled(" / BEDROCK LAB");
    ImGui::SetCursorPos({size.x-78*scale_,12*scale_});if(ImGui::Button("_",{29*scale_,27*scale_}))commands.minimize=true;ImGui::SameLine(0,4*scale_);
    if(ImGui::Button("X",{29*scale_,27*scale_}))commands.quit=true;
    float footerHeight=p_.ui.footer?38*scale_:8*scale_;navigation(63*scale_,size.y-63*scale_-footerHeight);
    const char* combat[]={"REACH","CLICKER","AIM ASSIST","HITBOX"};const char* movement[]={"VELOCITY","TIMER"};
    const char* visual[]={"ACCENT","COLOR","IMAGE","ZOOM","AUTO 360"};const char* settings[]={"HOTKEYS","PLUGS","DIAGNOSTICS"};
    const char** tabs=p_.ui.category==0?combat:p_.ui.category==1?movement:p_.ui.category==2?visual:settings;
    int count=std::array{4,2,5,3}[p_.ui.category];float tabX=95*scale_,tabY=64*scale_;
    for(int i=0;i<count;++i){
        float buttonWidth=ImGui::CalcTextSize(tabs[i]).x+style.FramePadding.x*2;
        if(i&&tabX+buttonWidth>size.x-13*scale_){tabX=95*scale_;tabY+=34*scale_;}
        ImGui::SetCursorPos({tabX,tabY});bool active=p_.ui.tabs[p_.ui.category]==i;
        if(active){ImGui::PushStyleColor(ImGuiCol_Button,accent_);ImGui::PushStyleColor(ImGuiCol_ButtonHovered,accent_);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,accent_);ImGui::PushStyleColor(ImGuiCol_Text,readableOn(accent_,rgba(p_.ui.activeText)));}
        if(ImGui::Button(tabs[i],{0,29*scale_})){p_.ui.tabs[p_.ui.category]=i;changed();}
        if(active)ImGui::PopStyleColor(4);
        tabX+=buttonWidth+4*scale_;
    }
    float contentTop=tabY+40*scale_;ImGui::SetCursorPos({95*scale_,contentTop});
    auto contentHeight=size.y-contentTop-footerHeight;
    ImGui::BeginChild("content",{size.x-108*scale_,contentHeight},ImGuiChildFlags_Borders|ImGuiChildFlags_AlwaysUseWindowPadding);
    int tab=p_.ui.tabs[p_.ui.category];
    if(p_.ui.category==0)modulePanel(std::array<const char*,4>{"reach","clicker","aim","hitbox"}[tab]);
    else if(p_.ui.category==1)modulePanel(tab==0?"velocity":"timer");
    else if(p_.ui.category==2){switch(tab){case 0:appearanceAccent();break;case 1:appearanceColor();break;case 2:appearanceImage();break;case 3:modulePanel("zoom");break;case 4:modulePanel("auto360");break;}}
    else {if(tab==0)hotkeyPanel();else if(tab==1)plugs();else diagnosticPanel();}
    ImGui::EndChild();
    if(p_.ui.footer){
        auto game=detector_.snapshot();auto runtime=input_.status();float y=size.y-29*scale_;
        draw->AddRectFilled({0,size.y-footerHeight+5*scale_},size,IM_COL32(5,6,12,240));
        ImGui::SetCursorPos({15*scale_,y});
        if(!message.empty()&&ImGui::GetTime()-messageChangedAt_<5){
            auto text=message;float available=size.x-170*scale_;
            while(text.size()>8&&ImGui::CalcTextSize(text.c_str()).x>available)text.resize(text.size()-1);
            if(text.size()<message.size())text+="...";
            ImGui::TextColored(accent_,"%s",text.c_str());help(message.c_str());
        }else{
            ImGui::TextColored(runtime.armed?ImVec4{1,.72f,.3f,1}:accent_,"%s",game.present?"GAME DETECTED / PARTIAL":"LOOKING FOR MINECRAFT");
            ImGui::SameLine();ImGui::TextDisabled("  F8 arm  /  F9 stop");
        }
        ImGui::SetCursorPos({size.x-135*scale_,y-2*scale_});if(ImGui::SmallButton("STOP INPUT"))input_.stop("Stopped from the footer.");
    }
    if(!message.empty()){
        ImGui::SetCursorPos({95*scale_,size.y-footerHeight-23*scale_});
        // Status details remain available without covering module controls.
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)&&ImGui::GetIO().KeyCtrl)ImGui::SetTooltip("%s",message.c_str());
    }
    if(ImGui::GetIO().KeyCtrl&&ImGui::IsKeyPressed(ImGuiKey_S,false))save();
    capturePopup();ImGui::End();
}
} // namespace aster
