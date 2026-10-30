#include "auth/license.h"
#include "integration/shared.h"
#include <TlHelp32.h>
#include <ShlObj.h>
#include <fstream>
#include <iostream>
#include <thread>

namespace ab=aster::bridge;
using namespace aster;
namespace {
std::uintptr_t remoteLoader(DWORD pid){
    auto local=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");MEMORY_BASIC_INFORMATION memory{};
    if(!VirtualQuery(reinterpret_cast<void*>(local),&memory,sizeof(memory)))throw std::runtime_error("Cannot resolve loader module");
    wchar_t path[MAX_PATH]{};GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),path,MAX_PATH);auto name=std::filesystem::path(path).filename().wstring();
    HANDLE list=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);if(list==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot enumerate game modules");
    MODULEENTRY32W module{};module.dwSize=sizeof(module);std::uintptr_t result=0;
    if(Module32FirstW(list,&module))do{if(_wcsicmp(module.szModule,name.c_str())==0){result=reinterpret_cast<std::uintptr_t>(module.modBaseAddr)+(reinterpret_cast<std::uintptr_t>(local)-reinterpret_cast<std::uintptr_t>(memory.AllocationBase));break;}}while(Module32NextW(list,&module));
    CloseHandle(list);if(!result)throw std::runtime_error("Remote loader module unavailable");return result;
}
Json actor(const ab::ActorData& data){return {{"entityId",data.entityId},{"position",{data.position.x,data.position.y,data.position.z}},
    {"velocity",{data.velocity.x,data.velocity.y,data.velocity.z}},{"rotation",{data.rotation.x,data.rotation.y}},{"width",data.width},{"height",data.height},{"type",data.type}};}
}
int wmain(int argc,wchar_t** argv){
    try{
        bool timerTest=argc>1&&std::wstring_view(argv[1])==L"--timer-test";
        unsigned seconds=timerTest?14:argc>1?static_cast<unsigned>(std::clamp(_wtoi(argv[1]),1,120)):10;
        auto game=GameDetector::detectOnce();if(!game.identityVerified||!game.x64||game.build!=kInputResearchBuild)throw std::runtime_error("Exact verified game package/build required");
        auto exe=std::filesystem::path(L"C:\\Program Files\\WindowsApps\\Microsoft.MinecraftUWP_1.26.6005.0_x64__8wekyb3d8bbwe\\Minecraft.Windows.exe");
        std::cout<<"Verifying exact Minecraft executable before read-only gameplay observation...\n";
        auto digest=sha256(readFile(exe,400*1024*1024));
        // Hash is compared to the verified build record supplied alongside the probe.
        auto expected=readFile(std::filesystem::current_path()/L"docs"/L"spec-review"/L"research"/L"game-sha256.txt",128);
        while(!expected.empty()&&isspace(static_cast<unsigned char>(expected.back())))expected.pop_back();
        if(digest!=expected)throw std::runtime_error("Game binary hash does not match the researched build");
        PWSTR local=nullptr;SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local);
        auto folder=std::filesystem::path(local)/L"Packages"/L"Microsoft.MinecraftUWP_8wekyb3d8bbwe"/L"LocalState"/L"AsterBridge";
        CoTaskMemFree(local);std::filesystem::create_directories(folder);
        auto dll=folder/L"AsterBridge.dll";std::filesystem::copy_file(std::filesystem::current_path()/L"build"/L"Release"/L"AsterBridge.dll",dll,std::filesystem::copy_options::overwrite_existing);
        auto session=folder/L"session.bin";HANDLE file=CreateFileW(session.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create task-owned shared session");
        HANDLE mapping=CreateFileMappingW(file,nullptr,PAGE_READWRITE,0,sizeof(ab::Shared),nullptr);if(!mapping)throw std::runtime_error("Cannot create shared mapping");
        auto shared=static_cast<ab::Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ab::Shared)));if(!shared)throw std::runtime_error("Cannot map shared session");
        memset(shared,0,sizeof(*shared));ab::Control control;control.session=GetTickCount64();control.heartbeat=GetTickCount64();
        ab::writeConsistent(shared->controlSequence,shared->control,control);
        HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|PROCESS_QUERY_INFORMATION|SYNCHRONIZE,FALSE,game.pid);
        if(!process)throw std::runtime_error("Windows denied access to this game process; no bypass is attempted");
        auto path=dll.wstring();std::size_t size=(path.size()+1)*sizeof(wchar_t);
        void* remote=VirtualAllocEx(process,nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);SIZE_T written=0;
        if(!remote||!WriteProcessMemory(process,remote,path.c_str(),size,&written)||written!=size)throw std::runtime_error("Cannot allocate task-owned loader argument");
        HANDLE thread=CreateRemoteThread(process,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoader(game.pid)),remote,0,nullptr);
        if(!thread)throw std::runtime_error("Windows refused the read-only observer thread");
        if(WaitForSingleObject(thread,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Observer loading timed out; loader argument retained to avoid a use-after-free");
        DWORD result=0;GetExitCodeThread(thread,&result);CloseHandle(thread);VirtualFreeEx(process,remote,0,MEM_RELEASE);
        if(!result)throw std::runtime_error("Windows could not load the observer DLL");
        auto started=GetTickCount64(),end=started+seconds*1000;ab::Telemetry last;Json samples=Json::array();
        while(GetTickCount64()<end){control.heartbeat=GetTickCount64();
            auto elapsed=(GetTickCount64()-started)/1000.;control.probeOnly=timerTest?0u:1u;control.timerEnabled=timerTest&&elapsed>=4&&elapsed<10;control.timerMultiplier=2;
            ab::writeConsistent(shared->controlSequence,shared->control,control);
            ab::Telemetry current;if(ab::readConsistent(shared->telemetrySequence,shared->telemetry,current))last=current;
            samples.push_back({{"seconds",elapsed},{"timerTps",last.timerApplied},{"localTps",last.measuredLocalTps},{"localTicks",last.localTicks},{"allActorTicks",last.allActorTicks},{"foreground",last.foreground},{"faults",last.layoutFailures}});
            std::cout<<"observer="<<static_cast<unsigned>(last.status)<<" updates="<<last.updateCalls<<" actors="<<last.actorCount<<" faults="<<last.layoutFailures<<" timer="<<last.timerApplied<<" observedTicks="<<last.measuredLocalTps<<'\n';Sleep(1000);}
        control.shutdown=1;control.heartbeat=GetTickCount64();ab::writeConsistent(shared->controlSequence,shared->control,control);Sleep(300);
        Json actors=Json::array();for(unsigned i=0;i<last.actorCount&&i<ab::MaxActors;++i)actors.push_back(actor(last.actors[i]));
        Json timers=Json::array();for(unsigned i=0;i<last.timerCount&&i<12;++i){const auto& timer=last.timers[i];timers.push_back({{"clientOffset",timer.clientOffset},{"minecraftOffset",timer.minecraftOffset},{"address",timer.address},{"values",timer.data}});}
        Json report={{"mode",timerTest?"Controlled timer test in disposable world":"gameplay data observation only; no module writes"},{"build",game.build},{"gameSha256",digest},
            {"status",static_cast<unsigned>(last.status)},{"errorStage",last.error},{"updates",last.updateCalls},{"faults",last.layoutFailures},
            {"localPlayer",actor(last.local)},{"actors",actors},{"timers",timers},{"gameModeVtableRvas",last.gameModeVtable},{"clientVtableRvas",last.clientVtable},{"samples",samples}};
        atomicWrite(std::filesystem::current_path()/L"test-results"/(timerTest?L"timer-world-test.json":L"engine-probe.json"),report.dump(2));
        UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(file);CloseHandle(process);
        std::cout<<"Read-only observer finished and shutdown requested.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
