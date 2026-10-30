#pragma once
#include "platform/windows.h"
#include <optional>
namespace aster {
struct HttpResponse {unsigned status=0;std::string body;};
HttpResponse httpRequest(std::string_view url,std::string_view method,std::string_view body={},
                         std::size_t maxBytes=2*1024*1024);
bool permittedServiceUrl(std::string_view url);
Json verifyEnvelope(const Json& envelope,std::string_view pem);
std::int64_t unixNow();

enum class LicenseState { LocalDevelopment, NotActivated, Activating, Activated, OfflineGrace, Rejected, Revoked, NetworkUnavailable };
std::string_view licenseStateName(LicenseState state);
struct LicenseSnapshot {
    LicenseState state=LicenseState::LocalDevelopment;
    std::string detail="Local developer build; no license service configured.",entitlement;
    std::int64_t expiresAt=0,offlineUntil=0;
    bool busy=false;
    bool entitled()const;
};
class CredentialVault {
public:
    explicit CredentialVault(std::wstring target);
    std::optional<std::string> read()const;
    void write(std::string_view secret)const;
    void erase()const;
private:
    std::wstring target_;
};
class LicenseClient {
public:
    LicenseClient(ServiceConfig,Diagnostics&);
    ~LicenseClient();
    void configure(ServiceConfig);
    bool activate(std::string key);
    bool refresh();
    void forget();
    LicenseSnapshot snapshot()const;
    bool configuredFor(const ServiceConfig&)const;
private:
    enum class Job {None,Resume,Activate,Refresh,Forget};
    void work(std::stop_token);
    void process(Job,std::string key,ServiceConfig,std::uint64_t generation);
    bool enqueue(Job,std::string key={});
    Diagnostics& log_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    ServiceConfig config_;
    LicenseSnapshot current_;
    Job job_=Job::Resume;
    std::string pendingKey_;
    std::uint64_t generation_=0;
    std::jthread worker_;
};
struct UpdateResult {
    bool available=false;
    std::string version,channel,url,sha256,signerThumbprint,notes;
    std::int64_t expiresAt=0;
};
UpdateResult checkUpdate(const UpdateConfig&);
std::filesystem::path downloadUpdate(const UpdateResult&,const std::filesystem::path& directory);
} // namespace aster
