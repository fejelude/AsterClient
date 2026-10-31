#include "auth/license.h"
#include "core/engine_math.h"
#include "integration/engine.h"
#include <Windows.h>
#include <wincred.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace aster;
namespace {
struct Suite {
    int passed=0,failed=0;Json cases=Json::array();
    void test(const char* name,const std::function<void()>& run){
        try{run();++passed;cases.push_back({{"name",name},{"passed",true}});std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++failed;cases.push_back({{"name",name},{"passed",false},{"error",e.what()}});std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
};
void require(bool truth,const char* message="requirement failed"){if(!truth)throw std::runtime_error(message);}
void expectNear(double actual,double expected,double tolerance=.000001){require(std::abs(actual-expected)<=tolerance,"numeric tolerance exceeded");}
template<class Fn> void throws(Fn fn){bool did=false;try{fn();}catch(...){did=true;}require(did,"expected failure did not occur");}
InputGate ready(){return {true,true,true,true,true};}
void pump(unsigned ms){
    auto until=GetTickCount64()+ms;do{MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}Sleep(1);}while(GetTickCount64()<until);
}
LicenseSnapshot waitLicense(LicenseClient& client){
    for(int i=0;i<100;++i){auto state=client.snapshot();if(!state.busy)return state;Sleep(100);}throw std::runtime_error("License worker timeout");
}
struct FixtureCount{int left=0,right=0,downs=0,ups=0;};
LRESULT CALLBACK fixtureProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto counts=reinterpret_cast<FixtureCount*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(counts){if(msg==WM_LBUTTONDOWN)++counts->left;if(msg==WM_RBUTTONDOWN)++counts->right;if(msg==WM_KEYDOWN)++counts->downs;if(msg==WM_KEYUP)++counts->ups;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int main(int argc,char** argv){
    auto root=std::filesystem::current_path()/L"test-results"/(L"native-"+wide(randomHex(6)));
    std::filesystem::create_directories(root);Suite s;
    if(argc==4&&std::string(argv[1]).rfind("--update-",0)==0){
        UpdateConfig config;config.feedUrl=argv[2];config.publicKeyPath=argv[3];
        if(std::string(argv[1])=="--update-integration")s.test("Native signed feed verification",[&]{auto update=checkUpdate(config);require(update.available);require(update.version=="0.2.0");});
        else s.test("Native altered feed rejection",[&]{throws([&]{(void)checkUpdate(config);});});
    }else if(argc==5&&std::string(argv[1]).rfind("--license-",0)==0){
        Diagnostics log(root/L"logs");ServiceConfig config{argv[2],argv[3],true};const std::string mode=argv[1];
        s.test("No entitlement before own-service initialization",[&]{LicenseClient client(config,log);require(!client.snapshot().entitled());waitLicense(client);});
        if(mode=="--license-prepare"){
            s.test("Real native HTTP activation and Windows vault persistence",[&]{
                LicenseClient client(config,log);waitLicense(client);auto key=readFile(wide(argv[4]),512);while(!key.empty()&&(key.back()=='\r'||key.back()=='\n'))key.pop_back();
                require(client.activate(key));SecureZeroMemory(key.data(),key.size());auto state=waitLicense(client);
                require(state.state==LicenseState::Activated,state.detail.c_str());require(state.entitled());require(state.entitlement=="lifetime");
                auto p=defaultProfile();p.license=config;auto text=profileToJson(p).dump();require(text.find("refreshToken")==std::string::npos);
            });
            s.test("Remembered activation after client process lifetime",[&]{LicenseClient resumed(config,log);auto state=waitLicense(resumed);require(state.state==LicenseState::Activated,state.detail.c_str());require(state.entitled());});
            s.test("Corrupt vault never grants an entitlement and can be forgotten",[&]{
                auto pem=readFile(wide(config.publicKeyPath),16384);CredentialVault vault(L"AsterClient/license/"+wide(sha256(config.endpoint+"\n"+pem)));
                auto original=vault.read();require(original.has_value());vault.write("{corrupt");
                {LicenseClient client(config,log);auto state=waitLicense(client);require(state.state==LicenseState::Rejected);require(!state.entitled());
                    client.forget();require(waitLicense(client).state==LicenseState::NotActivated);require(!vault.read());}
                vault.write(*original);SecureZeroMemory(original->data(),original->size());
            });
        }else if(mode=="--license-offline"){
            s.test("Signed offline grace while own service is stopped",[&]{LicenseClient resumed(config,log);auto state=waitLicense(resumed);require(state.state==LicenseState::OfflineGrace,state.detail.c_str());require(state.entitled());resumed.forget();require(waitLicense(resumed).state==LicenseState::NotActivated);});
        }
    }else{
        s.test("Fresh native gameplay capture works without a Win32 cursor clip",[]{
            GameSnapshot game;EngineRuntime engine;require(!gameplayCaptureReady(game,engine));
            engine.bound=engine.worldReady=true;engine.screen="gameplay_captured";require(gameplayCaptureReady(game,engine));
            engine.heartbeatAge=500;require(!gameplayCaptureReady(game,engine));engine.heartbeatAge=0;
            engine.screen="input_released";require(!gameplayCaptureReady(game,engine));engine.screen="gameplay_captured";engine.worldReady=false;require(!gameplayCaptureReady(game,engine));
            game.cursorCaptured=true;require(gameplayCaptureReady(game,engine));
        });
        s.test("Every module starts disabled and no engine capability reports active",[]{
            auto p=defaultProfile();GameSnapshot game;game.present=game.identityVerified=game.foreground=game.x64=true;game.build="1.26.6005.0";
            RuntimeStatus runtime;runtime.armed=true;
            for(const auto& d:moduleDefinitions()){auto& m=p.modules.at(d.id);require(!m.enabled);m.enabled=true;auto h=computeHealth(d,m,game,runtime);
                require(h.state!=ModuleState::Active);if(d.capability==Capability::Engine)require(h.state==ModuleState::Unsupported);}
        });
        s.test("Timer calculations include slow motion and the 1.06x discrepancy",[]{expectNear(timerTps(1),20);expectNear(timerTps(5),100);expectNear(timerBoost(1.06),6);expectNear(timerTps(.8),16);expectNear(timerBoost(.8),-20);expectNear(timerTps(0),2);expectNear(timerBoost(9),400);});
        s.test("Ray geometry distinguishes nearest-point distance, edge hits and behind-camera targets",[]{
            Box b{{-.3,-1,5},{.3,1,5.6}};expectNear(*rayBox({0,0,0},{0,0,1},b,5.2),5.);require(!rayBox({0,0,0},{0,0,1},b,4.99));
            require(!rayBox({1,0,0},{0,0,1},b,10));auto expanded=targetingBox(b,2.22);expectNear(*rayBox({1,0,0},{0,0,1},expanded,10),4.19);
            expectNear(nearestDistance({0,0,0},b),5);require(!rayBox({0,0,6},{0,0,1},b,10));require(rayBox({.3,0,0},{0,0,1},b,10).has_value());
            require(!rayBox({0,0,0},{0,0,1},{{1,1,1},{-1,-1,-1}},10));
        });
        s.test("Knockback axes, delta composition and backward projection are independent",[]{
            auto a=scaledImpulse({.2,.1,0},{1,.5,-1},.5,.25,false,false,0);expectNear(a.x,.6);expectNear(a.y,.2);expectNear(a.z,-.5);
            a=scaledImpulse({.2,.1,0},{1,.5,-1},.5,.25,true,false,0);expectNear(a.x,.5);expectNear(a.y,.125);
            a=scaledImpulse({},{1,.5,-1},1,1,false,true,0);expectNear(a.x,1);expectNear(a.z,0);expectNear(a.y,.5);
        });
        s.test("Aim smoothing preserves pitch in horizontal mode and respects the inner dead zone",[]{
            auto a=aimCorrection({179,17},{-170,-30},25,false,.016,true,false,.02);require(a.yaw!=179);expectNear(a.pitch,17);
            auto b=aimCorrection({0,5},{.5,5},25,false,.016,true,true,.02);expectNear(b.yaw,0);expectNear(b.pitch,5);
            auto fast=aimCorrection({0,0},{60,0},0,false,.016,true,false,0),slow=aimCorrection({0,0},{60,0},100,false,.016,true,false,0);require(fast.yaw>slow.yaw);
        });
        s.test("Runtime status requires a bound handler, live conditions and measured observations",[]{
            auto p=defaultProfile();p.modules.at("timer").enabled=true;GameSnapshot g;g.present=g.identityVerified=g.x64=g.foreground=true;g.build=std::string(kInputResearchBuild);
            RuntimeStatus r;auto& e=r.engine.modules[moduleIndex("timer")];require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Unsupported);
            r.engine.bound=e.bound=true;require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Bound);
            r.armed=r.engine.permitted=e.conditions=e.applied=true;require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Requested);
            e.observations=1;require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Observed);
            g.build="other";require(computeHealth(definition("timer"),p.modules.at("timer"),g,r).state==ModuleState::Unsupported);
        });
        s.test("Every live setting reaches the typed bridge configuration",[]{
            auto p=defaultProfile();p.modules.at("aim").enabled=true;p.modules.at("aim").number("height",.75);p.modules.at("velocity").number("vertical",63.12);p.modules.at("timer").number("multiplier",.8);
            auto c=bridgeConfiguration(p);require((c.enabled&moduleBit(EngineModule::Aim))!=0);expectNear(c.aimHeight,.75);expectNear(c.velocityVertical,.6312,.00001);expectNear(c.timerMultiplier,.8,.00001);require(c.armed==0);
        });
        s.test("Yaw wrap, vertical alignment and frame-independent angular speed",[]{
            expectNear(wrappedDegrees(359),-1);expectNear(lookAt({0,0,0},{0,0,5}).yaw,0);expectNear(lookAt({0,0,0},{0,5,0}).pitch,-90);
            auto a=Orientation{170,0};auto b2=a;for(int i=0;i<30;++i)a=smoothOrientation(a,{-170,30},60,1./30);
            for(int i=0;i<144;++i)b2=smoothOrientation(b2,{-170,30},60,1./144);expectNear(a.yaw,b2.yaw);expectNear(a.pitch,b2.pitch);
        });
        s.test("Target fixtures exclude self, friends, walls, bots and out-of-range entities",[]{
            std::vector<Target> targets{{1,{0,0,2},true},{2,{0,0,3},false,true},{3,{0,0,4},false,false,false},
                {4,{0,0,2},false,false,true,true},{5,{0,0,7}},{6,{0,0,4}},{7,{4,0,0}}};
            auto ids=eligibleTargets({0,0,0},{0,0},targets,{5,45,45,true,true});require(ids==std::vector<std::uint64_t>{6});
        });
        s.test("All settings and both independent Aim profiles round-trip",[]{
            auto p=defaultProfile();p.modules.at("aim").profiles.at("closet")["smoothness"]=17.;p.modules.at("aim").profiles.at("blatant")["smoothness"]=83.;
            p.ui.scale=1.4f;p.bindings["timer"]={'Y',2};std::vector<std::string> warnings;
            auto restored=profileFromJson(profileToJson(p),warnings);require(warnings.empty());require(profileToJson(restored)==profileToJson(p));
        });
        s.test("Representative PDF schema migration, clamping and safe unbinding",[]{
            Json j={{"schemaVersion",1},{"ui",{{"accentRGB",{0,210,255}},{"backgroundOpacity",.85}}},
                {"modules",{{"timer",{{"enabled",true},{"multiplier",99},{"combatMode",true}}}}},
                {"keybinds",{{"auto360","TAB"},{"timer",nullptr}}}, {"movementKeys",{{"sprint","W"},{"crouch","SHIFT"}}}};
            std::vector<std::string> warnings;auto p=profileFromJson(j,warnings);expectNear(p.modules.at("timer").number("multiplier"),5);
            require(p.modules.at("timer").enabled);require(p.bindings.at("auto360").key==VK_TAB);require(p.movement[4]=='W');require(!warnings.empty());
            p.bindings["clicker"]={VK_F9,0};validate(p);require(p.bindings.at("clicker").key==0);
        });
        s.test("Malformed types do not enable a feature",[]{
            auto j=profileToJson(defaultProfile());j["modules"]["clicker"]["profiles"]["default"]["randomize"]="yes";
            std::vector<std::string> warnings;auto p=profileFromJson(j,warnings);require(!p.modules.at("clicker").flag("randomize"));require(!warnings.empty());
        });
        s.test("Atomic profile saves survive reopening with a valid backup",[&]{
            auto path=root/L"atomic"/L"default.json";ConfigStore store(path);auto p=store.load().profile;store.save(p);
            p.modules.at("timer").number("multiplier",3.14);store.save(p);ConfigStore reopened(path);expectNear(reopened.load().profile.modules.at("timer").number("multiplier"),3.14);
            require(Json::parse(readFile(path.wstring()+L".bak")).at("schemaVersion")==3);
        });
        s.test("Corrupt profile is preserved and last valid backup recovers",[&]{
            auto path=root/L"corruption"/L"default.json";ConfigStore store(path);auto p=defaultProfile();p.modules.at("timer").number("multiplier",2);store.save(p);store.save(p);
            std::ofstream(path)<<"{invalid";auto loaded=store.load();require(loaded.recovered);expectNear(loaded.profile.modules.at("timer").number("multiplier"),2);store.save(loaded.profile);
            require(Json::parse(readFile(path.wstring()+L".bak")).at("schemaVersion")==3);
            bool preserved=false;for(const auto& f:std::filesystem::directory_iterator(path.parent_path()))if(f.path().filename().wstring().find(L".corrupt-")!=std::wstring::npos)preserved=true;require(preserved);
        });
        s.test("A future schema remains byte-for-byte preserved and read-only",[&]{
            auto path=root/L"future"/L"default.json";atomicWrite(path,"{\"schemaVersion\":99,\"future\":true}");auto before=readFile(path);
            ConfigStore store(path);require(store.load().readOnly);throws([&]{store.save(defaultProfile());});require(before==readFile(path));
        });
        s.test("Failed disk commit preserves the original profile",[&]{
            auto path=root/L"failure"/L"default.json";ConfigStore store(path);store.save(defaultProfile());auto before=readFile(path);
            HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);require(held!=INVALID_HANDLE_VALUE);
            bool failed=false;try{store.save(defaultProfile());}catch(...){failed=true;}CloseHandle(held);require(failed);require(before==readFile(path));
        });
        s.test("Hotkey conflicts, no-repeat matching and optional unbinding",[]{
            auto p=defaultProfile();p.bindings["timer"]={'R',2};p.bindings["reach"]={'R',2};require(bindingConflicts(p).size()==1);
            require(bindingMatches({'R',2},'R',2));require(!bindingMatches({'R',2},'R',0));require(!bindingMatches({},'R',0));
            require(!isSafeBinding({'A',0},true));require(isSafeBinding({VK_F10,0},true));require(!isSafeBinding({VK_F9,0},false));
        });
        s.test("Independent 10-second left/right CPS streams and immediate release",[]{
            auto p=defaultProfile();p.modules.at("clicker").enabled=true;PhysicalInput input;InputPlanner planner;planner.tick(0,p,input,ready());
            input.mouse={true,true};input.pressSequence={1,2};int counts[2]{};
            for(int i=1;i<=20000;++i){for(auto e:planner.tick(i*.0005,p,input,ready()))if(e.kind==OutputKind::Click)++counts[e.code];}
            require(std::abs(counts[0]-130)<=2);require(std::abs(counts[1]-300)<=4);input.mouse={};
            for(int i=0;i<100;++i)require(planner.tick(10+i*.01,p,input,ready()).empty());
        });
        s.test("Low polling rates preserve average CPS without catch-up bursts",[]{
            auto p=defaultProfile();auto& click=p.modules.at("clicker");click.enabled=true;click.number("leftCps",30);PhysicalInput input;InputPlanner planner;
            planner.tick(0,p,input,ready());input.mouse[0]=true;input.pressSequence[0]=1;int count=0;
            for(int i=1;i<=500;++i){auto events=planner.tick(i*.02,p,input,ready());auto clicks=std::count_if(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::Click;});require(clicks<=1);count+=static_cast<int>(clicks);}
            require(count>=298&&count<=300,"50 Hz polling lost the requested 30 CPS average");
            require(planner.tick(100,p,input,ready()).size()<=1);require(planner.tick(100.001,p,input,ready()).empty());
        });
        s.test("Focus loss disarms held buttons until a new physical press",[]{
            auto p=defaultProfile();p.modules.at("clicker").enabled=true;PhysicalInput input;InputPlanner planner;planner.tick(0,p,input,ready());input.mouse[0]=true;input.pressSequence[0]=1;
            planner.tick(.01,p,input,ready());require(!planner.tick(.2,p,input,ready()).empty());auto gate=ready();gate.foreground=false;
            require(planner.tick(.21,p,input,gate).empty());require(planner.tick(1,p,input,ready()).empty());require(planner.tick(2,p,input,ready()).empty());
            input.mouse[0]=false;planner.tick(2.01,p,input,ready());input.mouse[0]=true;input.pressSequence[0]=2;planner.tick(2.02,p,input,ready());require(!planner.tick(2.2,p,input,ready()).empty());
        });
        s.test("Priming follows physical event order and channels remain independent",[]{
            auto p=defaultProfile();auto& m=p.modules.at("clicker");m.enabled=true;m.flag("rightPriming",true);PhysicalInput input;InputPlanner planner;planner.tick(0,p,input,ready());
            input.mouse[1]=true;input.pressSequence[1]=1;planner.tick(.001,p,input,ready());require(planner.tick(.2,p,input,ready()).empty());
            input.mouse[0]=true;input.pressSequence[0]=2;planner.tick(.21,p,input,ready());auto events=planner.tick(.3,p,input,ready());
            require(std::any_of(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::Click&&e.code==1;}));
        });
        s.test("Native target gating controls Triggerbot without leaking into menus",[]{
            auto p=defaultProfile();p.modules.at("clicker").enabled=true;p.modules.at("clicker").flag("triggerbot",true);InputPlanner planner;PhysicalInput input;InputContext context;
            require(planner.tick(0,p,input,ready(),context).empty());context.entityTarget=true;
            require(!planner.tick(.1,p,input,ready(),context).empty());context.worldReady=false;require(planner.tick(.3,p,input,ready(),context).empty());
        });
        s.test("Auto and Legit mining preserve and release a single owned mouse hold",[]{
            for(bool legit:{false,true}){auto p=defaultProfile();auto& click=p.modules.at("clicker");click.enabled=true;click.flag(legit?"breakLegit":"breakAuto",true);PhysicalInput input;InputPlanner planner;InputContext context;
                planner.tick(0,p,input,ready(),context);input.mouse[0]=true;input.pressSequence[0]=1;context.blockTarget=true;context.blockToken=123;
                auto first=planner.tick(.01,p,input,ready(),context);require(std::any_of(first.begin(),first.end(),[](auto e){return e.kind==OutputKind::MouseDown;})==!legit);
                auto later=planner.tick(.3,p,input,ready(),context);if(legit)require(std::any_of(later.begin(),later.end(),[](auto e){return e.kind==OutputKind::MouseDown;}));
                auto release=planner.stop(input);require(std::any_of(release.begin(),release.end(),[](auto e){return e.kind==OutputKind::MouseUp;}));require(planner.stop(input).empty());
            }
        });
        s.test("Inverted mining follows the logical attack stream",[]{
            auto p=defaultProfile();auto& click=p.modules.at("clicker");click.enabled=true;click.flag("invert",true);click.flag("breakAuto",true);PhysicalInput input;InputPlanner planner;InputContext context;context.blockTarget=true;
            planner.tick(0,p,input,ready(),context);input.mouse[1]=true;input.pressSequence[1]=1;auto events=planner.tick(.01,p,input,ready(),context);
            require(std::any_of(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::MouseDown&&e.code==0;}));
            require(std::none_of(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::MouseDown&&e.code==1;}));
        });
        s.test("S-Tap uses an observed attack and restores forward after 60 ms",[]{
            auto p=defaultProfile();p.modules.at("stap").enabled=true;InputPlanner planner;PhysicalInput input;input.keys['W']=true;InputContext context;
            require(planner.tick(0,p,input,ready(),context).empty());context.attackSequence=1;auto e=planner.tick(.01,p,input,ready(),context);
            require(std::any_of(e.begin(),e.end(),[](auto v){return v.kind==OutputKind::KeyUp&&v.code=='W';}));require(std::any_of(e.begin(),e.end(),[](auto v){return v.kind==OutputKind::KeyDown&&v.code=='S';}));
            e=planner.tick(.071,p,input,ready(),context);require(std::any_of(e.begin(),e.end(),[](auto v){return v.kind==OutputKind::KeyUp&&v.code=='S';}));require(std::any_of(e.begin(),e.end(),[](auto v){return v.kind==OutputKind::KeyDown&&v.code=='W';}));
            require(planner.tick(.1,p,input,ready(),context).empty());
        });
        s.test("S-Tap cancellation restores held forward only for an explicit in-world stop",[]{
            for(int mode=0;mode<3;++mode){
                auto p=defaultProfile();p.modules.at("stap").enabled=true;InputPlanner planner;PhysicalInput input;input.keys['W']=true;InputContext context;
                planner.tick(0,p,input,ready(),context);context.attackSequence=1;planner.tick(.01,p,input,ready(),context);
                if(mode==2)input.keys['W']=false;
                auto gate=ready();gate.armed=false;context.restorePhysical=mode!=0;
                auto events=planner.tick(.02,p,input,gate,context);
                require(std::any_of(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::KeyUp&&e.code=='S';}));
                require(std::any_of(events.begin(),events.end(),[](auto e){return e.kind==OutputKind::KeyDown&&e.code=='W';})==(mode==1));
                require(planner.tick(.03,p,input,gate,context).empty());
            }
        });
        s.test("Randomized intervals vary within bounds with a declared average",[]{
            auto p=defaultProfile();auto& m=p.modules.at("clicker");m.enabled=true;m.flag("randomize",true);PhysicalInput input;InputPlanner planner(123);planner.tick(0,p,input,ready());
            input.mouse[0]=true;input.pressSequence[0]=1;std::vector<double> times;
            for(int i=1;i<40000;++i)for(auto e:planner.tick(i*.0005,p,input,ready()))if(e.kind==OutputKind::Click)times.push_back(i*.0005);
            double lo=10,hi=0,total=0;for(std::size_t i=1;i<times.size();++i){double dt=times[i]-times[i-1];lo=std::min(lo,dt);hi=std::max(hi,dt);total+=dt;}
            require(hi-lo>.01);require(lo>=.88/13.-.001);require(hi<=1.12/13.+.001);expectNear(total/(times.size()-1),1./13.,.003);
        });
        s.test("Every cancellation gate releases only owned synthetic keys",[]{
            for(int i=0;i<5;++i){auto p=defaultProfile();p.modules.at("walk").enabled=true;PhysicalInput input;InputPlanner planner;auto events=planner.tick(0,p,input,ready());require(events.size()==1&&events[0].kind==OutputKind::KeyDown);
                auto gate=ready();if(i==0)gate.armed=false;if(i==1)gate.gameVerified=false;if(i==2)gate.foreground=false;if(i==3)gate.cursorReady=false;if(i==4)gate.licensed=false;
                events=planner.tick(.01,p,input,gate);require(events.size()==1&&events[0].kind==OutputKind::KeyUp);require(planner.tick(.02,p,input,gate).empty());}
        });
        s.test("Physical key ownership, duplicate movement map and delayed frames",[]{
            auto p=defaultProfile();p.modules.at("walk").enabled=true;p.modules.at("sprint").enabled=true;p.movement[4]='W';PhysicalInput input;InputPlanner planner;
            require(planner.tick(0,p,input,ready()).size()==1);input.keys['W']=true;auto events=planner.stop(input);require(events.empty());
            p.modules.at("walk").enabled=false;p.modules.at("sprint").enabled=false;p.modules.at("clicker").enabled=true;planner.reset();input={};planner.tick(0,p,input,ready());input.mouse={true,true};input.pressSequence={1,2};planner.tick(.01,p,input,ready());events=planner.tick(30,p,input,ready());require(events.size()==2);
        });
        s.test("Credential Manager persists secrets outside JSON and supports removal",[]{
            auto target=L"AsterClient/test/"+wide(randomHex(16));CredentialVault vault(target);auto secret=randomHex(32);require(!vault.read());vault.write(secret);require(vault.read()==secret);
            PCREDENTIALW raw=nullptr;require(CredReadW(target.c_str(),CRED_TYPE_GENERIC,0,&raw)!=FALSE);require(raw->Persist==CRED_PERSIST_LOCAL_MACHINE);CredFree(raw);vault.erase();require(!vault.read());
        });
        s.test("Process detector verifies real package identity and never claims full integration",[]{
            auto game=GameDetector::detectOnce();require(game.state!=ConnectionState::Connected);
            if(game.identityVerified){require(game.present&&game.x64);require(game.packageFamily=="Microsoft.MinecraftUWP_8wekyb3d8bbwe");require(!game.build.empty());}
            require(!verifiedInputBuild("future-build"));
        });
        if(argc>1&&std::string(argv[1])=="--input-fixture")s.test("Native fixture receives clicks/keys while injected events cannot prime physical input",[]{
            WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=fixtureProc;wc.lpszClassName=L"AsterInputTestFixture";RegisterClassW(&wc);
            FixtureCount count;HWND window=CreateWindowW(wc.lpszClassName,L"Aster input test fixture",WS_OVERLAPPEDWINDOW,120,120,320,240,nullptr,nullptr,wc.hInstance,nullptr);
            require(window!=nullptr);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&count));
            ShowWindow(window,SW_SHOWNORMAL);SetForegroundWindow(window);
            auto until=GetTickCount64()+60000;while(!windowBelongsTo(GetForegroundWindow(),GetCurrentProcessId())&&GetTickCount64()<until)pump(50);pump(60);
            require(windowBelongsTo(GetForegroundWindow(),GetCurrentProcessId()),"Fixture could not acquire foreground; no test input sent");
            InputObserver observer;require(observer.healthy());POINT client{80,80};ClientToScreen(window,&client);SetCursorPos(client.x,client.y);pump(10);
            require(InputService::sendEvent({OutputKind::Click,0,"test"}));require(InputService::sendEvent({OutputKind::Click,1,"test"}));
            require(InputService::sendEvent({OutputKind::KeyDown,'G',"test"}));require(InputService::sendEvent({OutputKind::KeyUp,'G',"test"}));pump(80);
            auto physical=observer.snapshot();require(count.left==1&&count.right==1,"Fixture did not receive independent click events");require(count.downs>=1&&count.ups>=1);
            require(!physical.mouse[0]&&!physical.mouse[1]&&!physical.keys['G']);require(physical.eventSequence==0,"Injected input entered the physical observer");
            DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
        });
        s.test("Diagnostics redact secret assignments and paths and never export input history",[&]{
            Diagnostics log(root/L"redaction");log.event("test","TEST",R"(refreshToken=secret123 path C:\Users\Someone\private.json)");
            auto report=log.report(defaultProfile(),{},{});require(report.find("secret123")==std::string::npos);require(report.find("Someone")==std::string::npos);require(report.find("gameAcknowledgments")!=std::string::npos);
        });
        s.test("Untrusted network URLs and invalid signed updates fail closed",[]{
            require(permittedServiceUrl("https://example.com/api"));require(permittedServiceUrl("http://127.0.0.1:38471"));
            require(!permittedServiceUrl("http://example.com"));require(!permittedServiceUrl("https://user:password@example.com"));
            require(!permittedServiceUrl("file:///C:/secret"));require(!verifyRsaSha256("payload","ZmFrZQ","invalid pem"));
            throws([]{verifyEnvelope(Json{{"payload","e30"},{"signature","ZmFrZQ"}},"invalid");});
        });
    }
    auto result=Json{{"passed",s.passed},{"failed",s.failed},{"cases",s.cases}};atomicWrite(root/L"results.json",result.dump(2),false);
    std::cout<<s.passed<<" passed, "<<s.failed<<" failed\n";return s.failed?1:0;
}
