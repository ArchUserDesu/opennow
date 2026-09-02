#pragma once
#include "http_client.hpp"
#include "models.hpp"
#include <functional>
#include <string>
#include <vector>
namespace opennow {
class GfnClient {
public:
    std::vector<LoginProvider> fetch_login_providers() const;
    AuthSession login_qr(const LoginProvider&,
                         const std::function<void(const QrLoginChallenge&)>&,
                         const std::function<bool()>& cancel = std::function<bool()>()) const;
    AuthSession refresh(AuthSession) const;
    std::vector<GameInfo> fetch_public_games(const std::string& filter = std::string()) const;
    std::vector<GameInfo> fetch_catalog_games(AuthSession&, const std::string& search = std::string()) const;
    SessionInfo start_session(AuthSession&, const GameInfo&, const StreamConfig&) const;
    SessionInfo poll_session(AuthSession&, const std::string&) const;
    void stop_session(AuthSession&, const std::string&) const;
    static std::string session_jwt(const AuthSession& s);
private:
    HttpClient http_;
};
}
