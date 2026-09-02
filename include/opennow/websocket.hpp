#pragma once
#include <string>
#include <vector>
namespace opennow {
class WebSocket {
public:
    WebSocket(); ~WebSocket();
    bool connect(const std::string& url,const std::vector<std::string>& headers);
    bool send_text(const std::string& text);
    bool poll(std::vector<std::string>& messages);
    void close();
    const std::string& error() const { return error_; }
private:
#if defined(OPENNOW_XDK)
    void* xdk_state_;
#else
    void* curl_; void* headers_;
#endif
    std::string rx_,error_; bool connected_;
};
}
