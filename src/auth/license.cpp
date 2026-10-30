#include "auth/license.h"
#include <wincred.h>
#include <winhttp.h>
#include <chrono>
#include <regex>
#include <stdexcept>

namespace aster {
namespace {
struct Internet {
    HINTERNET handle=nullptr;
    explicit Internet(HINTERNET h):handle(h){}
    ~Internet(){if(handle)WinHttpCloseHandle(handle);}
};
struct Wipe {
    std::string& value;
    ~Wipe(){if(!value.empty())SecureZeroMemory(value.data(),value.size());value.clear();}
};
struct ParsedUrl {std::wstring host,path;INTERNET_PORT port=0;bool secure=false;};
ParsedUrl parseUrl(std::string_view url){
    if(url.size()>4096)throw std::runtime_error("URL_TOO_LONG");
    auto w=wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(w.c_str(),static_cast<DWORD>(w.size()),0,&parts)||!parts.dwHostNameLength||parts.dwUserNameLength||parts.dwPasswordLength)
        throw std::runtime_error("INVALID_SERVICE_URL");
    ParsedUrl result;result.host.assign(parts.lpszHostName,parts.dwHostNameLength);
    result.path.assign(parts.lpszUrlPath,parts.dwUrlPathLength);if(result.path.empty())result.path=L"/";
    if(parts.dwExtraInfoLength)result.path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    if(result.path.find(L'#')!=std::wstring::npos)throw std::runtime_error("URL_FRAGMENT_NOT_ALLOWED");
    result.secure=parts.nScheme==INTERNET_SCHEME_HTTPS;result.port=parts.nPort;
    if(!result.secure&&!(parts.nScheme==INTERNET_SCHEME_HTTP&&result.host==L"127.0.0.1"))
        throw std::runtime_error("HTTPS_REQUIRED");
    return result;
}
std::string endpointBase(std::string endpoint){while(!endpoint.empty()&&endpoint.back()=='/')endpoint.pop_back();return endpoint;}
void verifyEntitlement(const Json& claims,std::string_view nonce,std::int64_t now){
    if(claims.value("iss","")!="aster-license"||claims.value("aud","")!="aster-client"||claims.value("schema",0)!=1)
        throw std::runtime_error("ENTITLEMENT_AUDIENCE_INVALID");
    auto issued=claims.at("iat").get<std::int64_t>(),expiry=claims.at("exp").get<std::int64_t>(),offline=claims.at("offlineUntil").get<std::int64_t>();
    if(issued>now+60||expiry<=issued||expiry-issued>1800||offline<expiry||offline-issued>86400)
        throw std::runtime_error("ENTITLEMENT_TIME_INVALID");
    if(!nonce.empty()&&claims.value("nonce","")!=nonce)throw std::runtime_error("RESPONSE_NONCE_MISMATCH");
    if(!claims.at("keyExpiresAt").is_null()&&claims.at("keyExpiresAt").get<std::int64_t>()<offline)
        throw std::runtime_error("ENTITLEMENT_EXPIRY_INVALID");
    auto entitlement=claims.at("entitlement").get<std::string>();
    if(entitlement!="lifetime"&&entitlement!="subscription")throw std::runtime_error("ENTITLEMENT_CLASS_INVALID");
}
std::array<int,3> semver(std::string_view value){
    static const std::regex pattern(R"(^([0-9]{1,4})\.([0-9]{1,4})\.([0-9]{1,4})(?:-[a-zA-Z0-9.]+)?$)");
    std::cmatch match;
    if(!std::regex_match(value.data(),value.data()+value.size(),match,pattern))throw std::runtime_error("INVALID_RELEASE_VERSION");
    return {std::stoi(match[1]),std::stoi(match[2]),std::stoi(match[3])};
}
}
std::int64_t unixNow(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
bool permittedServiceUrl(std::string_view url){try{(void)parseUrl(url);return true;}catch(...){return false;}}
HttpResponse httpRequest(std::string_view url,std::string_view method,std::string_view body,std::size_t maxBytes){
    auto parts=parseUrl(url);auto verb=wide(method);
    if(body.size()>16384)throw std::runtime_error("REQUEST_TOO_LARGE");
    Internet session(WinHttpOpen(L"AsterClient/0.1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session.handle)throw std::runtime_error("NETWORK_UNAVAILABLE");
    WinHttpSetTimeouts(session.handle,5000,5000,5000,5000);
    Internet connection(WinHttpConnect(session.handle,parts.host.c_str(),parts.port,0));
    if(!connection.handle)throw std::runtime_error("NETWORK_UNAVAILABLE");
    Internet request(WinHttpOpenRequest(connection.handle,verb.c_str(),parts.path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,parts.secure?WINHTTP_FLAG_SECURE:0));
    if(!request.handle)throw std::runtime_error("NETWORK_UNAVAILABLE");
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.handle,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    const wchar_t* headers=L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if(!WinHttpSendRequest(request.handle,headers,static_cast<DWORD>(-1),body.empty()?WINHTTP_NO_REQUEST_DATA:const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),static_cast<DWORD>(body.size()),0)||!WinHttpReceiveResponse(request.handle,nullptr))throw std::runtime_error("NETWORK_UNAVAILABLE");
    HttpResponse response;DWORD size=sizeof(response.status);
    if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&response.status,&size,nullptr))throw std::runtime_error("HTTP_STATUS_UNAVAILABLE");
    for(;;){DWORD available=0;if(!WinHttpQueryDataAvailable(request.handle,&available))throw std::runtime_error("NETWORK_READ_FAILED");
        if(!available)break;if(response.body.size()+available>maxBytes)throw std::runtime_error("RESPONSE_TOO_LARGE");
        auto offset=response.body.size();response.body.resize(offset+available);DWORD got=0;
        if(!WinHttpReadData(request.handle,response.body.data()+offset,available,&got))throw std::runtime_error("NETWORK_READ_FAILED");
        response.body.resize(offset+got);if(!got)break;}
    return response;
}
Json verifyEnvelope(const Json& envelope,std::string_view pem){
    auto payload=envelope.at("payload").get<std::string>(),signature=envelope.at("signature").get<std::string>();
    if(payload.size()>128*1024||!verifyRsaSha256(payload,signature,pem))throw std::runtime_error("SIGNATURE_INVALID");
    auto decoded=base64Decode(payload);return Json::parse(decoded.begin(),decoded.end());
}
std::string_view licenseStateName(LicenseState s){
    switch(s){case LicenseState::LocalDevelopment:return "LOCAL DEVELOPMENT";case LicenseState::NotActivated:return "NOT ACTIVATED";
    case LicenseState::Activating:return "ACTIVATING";case LicenseState::Activated:return "ACTIVATED";case LicenseState::OfflineGrace:return "OFFLINE GRACE";
    case LicenseState::Rejected:return "REJECTED";case LicenseState::Revoked:return "REVOKED";case LicenseState::NetworkUnavailable:return "NETWORK UNAVAILABLE";}return "UNKNOWN";
}
bool LicenseSnapshot::entitled()const{return state==LicenseState::LocalDevelopment||((state==LicenseState::Activated||state==LicenseState::OfflineGrace)&&offlineUntil>=unixNow());}
CredentialVault::CredentialVault(std::wstring target):target_(std::move(target)){}
std::optional<std::string> CredentialVault::read()const{
    PCREDENTIALW credential=nullptr;
    if(!CredReadW(target_.c_str(),CRED_TYPE_GENERIC,0,&credential)){
        if(GetLastError()==ERROR_NOT_FOUND)return std::nullopt;throw std::runtime_error("VAULT_READ_FAILED");}
    std::string result(reinterpret_cast<char*>(credential->CredentialBlob),credential->CredentialBlobSize);
    SecureZeroMemory(credential->CredentialBlob,credential->CredentialBlobSize);CredFree(credential);return result;
}
void CredentialVault::write(std::string_view secret)const{
    if(secret.size()>CRED_MAX_CREDENTIAL_BLOB_SIZE)throw std::runtime_error("VAULT_BLOB_TOO_LARGE");
    CREDENTIALW credential{};credential.Type=CRED_TYPE_GENERIC;credential.TargetName=const_cast<wchar_t*>(target_.c_str());
    credential.CredentialBlobSize=static_cast<DWORD>(secret.size());credential.CredentialBlob=reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    credential.Persist=CRED_PERSIST_LOCAL_MACHINE;credential.UserName=const_cast<wchar_t*>(L"Aster own-service session");
    if(!CredWriteW(&credential,0))throw std::runtime_error("VAULT_WRITE_FAILED");
}
void CredentialVault::erase()const{if(!CredDeleteW(target_.c_str(),CRED_TYPE_GENERIC,0)&&GetLastError()!=ERROR_NOT_FOUND)throw std::runtime_error("VAULT_DELETE_FAILED");}
LicenseClient::LicenseClient(ServiceConfig config,Diagnostics& log):log_(log),config_(std::move(config)),
    current_([this]{LicenseSnapshot s;s.state=config_.endpoint.empty()&&!config_.required?LicenseState::LocalDevelopment:LicenseState::NotActivated;s.busy=true;return s;}()),
    worker_([this](std::stop_token t){work(t);}){}
LicenseClient::~LicenseClient(){worker_.request_stop();wake_.notify_all();if(worker_.joinable())worker_.join();if(!pendingKey_.empty())SecureZeroMemory(pendingKey_.data(),pendingKey_.size());}
void LicenseClient::configure(ServiceConfig config){
    std::lock_guard lock(mutex_);++generation_;config_=std::move(config);current_={};current_.busy=true;
    current_.state=config_.endpoint.empty()&&!config_.required?LicenseState::LocalDevelopment:LicenseState::NotActivated;
    job_=Job::Resume;if(!pendingKey_.empty())SecureZeroMemory(pendingKey_.data(),pendingKey_.size());pendingKey_.clear();wake_.notify_all();
}
bool LicenseClient::enqueue(Job job,std::string key){
    std::lock_guard lock(mutex_);if(current_.busy||job_!=Job::None){Wipe wipe{key};return false;}
    job_=job;pendingKey_=std::move(key);current_.busy=true;
    if(job==Job::Activate)current_.state=LicenseState::Activating;wake_.notify_all();return true;
}
bool LicenseClient::activate(std::string key){return enqueue(Job::Activate,std::move(key));}
bool LicenseClient::refresh(){return enqueue(Job::Refresh);}
void LicenseClient::forget(){enqueue(Job::Forget);}
LicenseSnapshot LicenseClient::snapshot()const{
    std::lock_guard lock(mutex_);auto result=current_;
    if((result.state==LicenseState::Activated||result.state==LicenseState::OfflineGrace)&&result.offlineUntil<unixNow()){
        result.state=LicenseState::NotActivated;result.detail="Offline grace expired; reconnect to your own license service.";}
    return result;
}
bool LicenseClient::configuredFor(const ServiceConfig& config)const{
    std::lock_guard lock(mutex_);return config.endpoint==config_.endpoint&&config.publicKeyPath==config_.publicKeyPath&&config.required==config_.required;
}
void LicenseClient::work(std::stop_token token){
    auto lastAttempt=std::chrono::steady_clock::now();
    while(!token.stop_requested()){
        Job job;std::string key;ServiceConfig config;std::uint64_t generation;
        {std::unique_lock lock(mutex_);wake_.wait_for(lock,token,std::chrono::seconds(30),[&]{return job_!=Job::None;});
            if(token.stop_requested())break;
            if(job_==Job::None){
                bool due=current_.state==LicenseState::Activated&&current_.expiresAt-unixNow()<120;
                bool retry=current_.state==LicenseState::OfflineGrace&&std::chrono::steady_clock::now()-lastAttempt>std::chrono::minutes(5);
                if(!due&&!retry)continue;job_=Job::Refresh;
            }
            job=job_;job_=Job::None;key=std::move(pendingKey_);config=config_;generation=generation_;current_.busy=true;}
        lastAttempt=std::chrono::steady_clock::now();process(job,std::move(key),std::move(config),generation);
    }
}
void LicenseClient::process(Job job,std::string key,ServiceConfig config,std::uint64_t generation){
    Wipe wipeKey{key};LicenseSnapshot result;result.state=LicenseState::NotActivated;
    Json cached,claims;std::string pem;std::optional<CredentialVault> vault;
    try{
        config.endpoint=endpointBase(config.endpoint);
        if(config.endpoint.empty()){
            result.state=config.required?LicenseState::NotActivated:LicenseState::LocalDevelopment;
            result.detail=config.required?"Configure your own service endpoint and public key.":"Local developer build; no license service configured.";
        }else{
            if(!permittedServiceUrl(config.endpoint))throw std::runtime_error("HTTPS_REQUIRED");
            pem=readFile(wide(config.publicKeyPath),16384);
            vault.emplace(L"AsterClient/license/"+wide(sha256(config.endpoint+"\n"+pem)));
            if(job!=Job::Forget)if(auto record=vault->read()){
                Wipe wipeRecord{*record};cached=Json::parse(*record);
                claims=verifyEnvelope(cached.at("envelope"),pem);verifyEntitlement(claims,"",unixNow());
                if(cached.at("lastSeen").get<std::int64_t>()>unixNow()+60)throw std::runtime_error("CLOCK_ROLLBACK_DETECTED");
            }
            if(job==Job::Forget){vault->erase();result.detail="Local session removed from Windows Credential Manager.";}
            else if(job!=Job::Activate&&cached.is_null()){
                result.detail="Enter a key issued by your own license service.";
            }else{
                const auto nonce=randomHex(24);
                Json request={{"nonce",nonce},{"clientVersion",kVersion}};
                bool activation=job==Job::Activate;
                if(activation){if(key.empty()||key.size()>256)throw std::runtime_error("KEY_FORMAT_INVALID");request["key"]=key;}
                else request["refreshToken"]=cached.at("refreshToken");
                std::string body=request.dump();Wipe wipeBody{body};
                if(activation){auto& raw=request["key"].get_ref<std::string&>();SecureZeroMemory(raw.data(),raw.size());}
                else {auto& raw=request["refreshToken"].get_ref<std::string&>();SecureZeroMemory(raw.data(),raw.size());}
                auto response=httpRequest(config.endpoint+(activation?"/api/v1/activate":"/api/v1/refresh"),"POST",body);
                Wipe wipeResponse{response.body};
                if(response.status>=500||response.status==429)throw std::runtime_error("NETWORK_UNAVAILABLE");
                if(response.status!=200){
                    std::string errorCode;try{errorCode=Json::parse(response.body).value("error","");}catch(...){}
                    bool revoked=errorCode=="LICENSE_REVOKED"||errorCode=="LICENSE_EXPIRED";
                    if(!activation||revoked)vault->erase();
                    result.state=revoked?LicenseState::Revoked:LicenseState::Rejected;
                    result.detail=revoked?"Your own service revoked or expired this license.":"Your own service rejected this key or session.";
                }else{
                    auto document=Json::parse(response.body);auto newClaims=verifyEnvelope(document.at("envelope"),pem);
                    verifyEntitlement(newClaims,nonce,unixNow());
                    if(newClaims.at("exp").get<std::int64_t>()<=unixNow())throw std::runtime_error("ENTITLEMENT_ALREADY_EXPIRED");
                    auto refresh=document.at("refreshToken").get<std::string>();Wipe wipeRefresh{refresh};
                    if(refresh.size()<32||refresh.size()>256)throw std::runtime_error("REFRESH_TOKEN_INVALID");
                    auto secure=Json{{"refreshToken",refresh},{"envelope",document.at("envelope")},{"lastSeen",unixNow()}}.dump();Wipe wipeSecure{secure};
                    vault->write(secure);claims=std::move(newClaims);
                    result.state=LicenseState::Activated;result.entitlement=claims.at("entitlement");
                    result.expiresAt=claims.at("exp");result.offlineUntil=claims.at("offlineUntil");
                    result.detail="Signed entitlement verified. Refresh session stored in Windows Credential Manager.";
                }
            }
        }
    }catch(const std::exception& e){
        auto error=std::string(e.what());
        const bool network=error=="NETWORK_UNAVAILABLE"||error=="NETWORK_READ_FAILED"||error=="HTTP_STATUS_UNAVAILABLE";
        bool grace=false;
        if(network&&!claims.is_null())try{
            verifyEntitlement(claims,"",unixNow());
            if(claims.at("offlineUntil").get<std::int64_t>()>unixNow()){
                result.state=LicenseState::OfflineGrace;result.entitlement=claims.at("entitlement");
                result.expiresAt=claims.at("exp");result.offlineUntil=claims.at("offlineUntil");
                result.detail="Service unavailable; previously signed offline grace is valid.";grace=true;
            }
        }catch(...){}
        if(!grace){result.state=network?LicenseState::NetworkUnavailable:LicenseState::Rejected;
            result.detail=network?"Own license service unavailable. No valid offline entitlement is cached.":
                "License configuration, signature, clock or vault validation failed. Review the endpoint and public key.";}
        log_.event("license",network?"NETWORK_UNAVAILABLE":"LICENSE_VALIDATION_FAILED",result.detail);
    }
    if(cached.contains("refreshToken")&&cached["refreshToken"].is_string()){
        auto& raw=cached["refreshToken"].get_ref<std::string&>();SecureZeroMemory(raw.data(),raw.size());}
    result.busy=false;
    {std::lock_guard lock(mutex_);if(generation==generation_)current_=result;}
    log_.event("license","LICENSE_STATE",licenseStateName(result.state));
}
UpdateResult checkUpdate(const UpdateConfig& config){
    if(config.feedUrl.empty()||config.publicKeyPath.empty())throw std::runtime_error("Configure your own signed update feed and public key.");
    auto response=httpRequest(config.feedUrl,"GET");if(response.status!=200)throw std::runtime_error("Update feed request failed.");
    auto claims=verifyEnvelope(Json::parse(response.body),readFile(wide(config.publicKeyPath),16384));
    if(claims.value("schema",0)!=1||claims.value("kind","")!="aster-release"||claims.value("platform","")!="windows-x64"||claims.value("channel","")!=config.channel)
        throw std::runtime_error("Release feed identity or channel mismatch.");
    auto now=unixNow(),issued=claims.at("issuedAt").get<std::int64_t>(),expires=claims.at("expiresAt").get<std::int64_t>();
    if(issued>now+60||expires<=now||expires-issued>31*86400)throw std::runtime_error("Release feed expired or has invalid dates.");
    UpdateResult result;result.version=claims.at("version");result.channel=config.channel;
    result.available=semver(result.version)>semver(kVersion);result.url=claims.at("url");result.sha256=claims.at("sha256");
    result.notes=claims.value("notes","");result.expiresAt=expires;
    static const std::regex digest("^[a-fA-F0-9]{64}$");
    if(!std::regex_match(result.sha256,digest)||!permittedServiceUrl(result.url)||result.url.rfind("https://",0)!=0)
        throw std::runtime_error("Invalid release package hash or HTTPS URL.");
    result.signerThumbprint=config.signerThumbprint;
    if(result.notes.size()>8000)throw std::runtime_error("Release notes too large.");return result;
}
std::filesystem::path downloadUpdate(const UpdateResult& update,const std::filesystem::path& directory){
    if(!update.available||update.expiresAt<=unixNow())throw std::runtime_error("No current release is available.");
    (void)semver(update.version);
    if(update.signerThumbprint.size()!=64)throw std::runtime_error("Configure the trusted publisher certificate SHA-256 before downloading.");
    auto response=httpRequest(update.url,"GET",{},128*1024*1024);
    if(response.status!=200||sha256(response.body)!=update.sha256)throw std::runtime_error("Release download or SHA-256 verification failed.");
    auto path=directory/(L"AsterClient-"+wide(update.version)+L".exe");
    atomicWrite(path,response.body,false);
    if(!trustedExecutable(path,update.signerThumbprint)){
        std::error_code error;std::filesystem::remove(path,error);throw std::runtime_error("Release Authenticode signature or publisher pin is invalid.");}
    return path;
}
} // namespace aster
