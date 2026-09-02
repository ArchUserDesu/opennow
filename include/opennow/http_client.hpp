#pragma once
#include <string>
#include <vector>
namespace opennow {
struct HttpResponse {
    long status_code;
    std::string body;
    std::string error;
    HttpResponse():status_code(0){}
};
class HttpClient {
public:
    HttpClient();
    ~HttpClient();
    HttpResponse request(const std::string& method,
                         const std::string& url,
                         const std::vector<std::string>& headers = std::vector<std::string>(),
                         const std::string& body = std::string()) const;
    HttpResponse get(const std::string& url,
                     const std::vector<std::string>& headers = std::vector<std::string>()) const {
        return request("GET", url, headers, std::string());
    }
    HttpResponse post(const std::string& url,
                      const std::vector<std::string>& headers,
                      const std::string& body) const {
        return request("POST", url, headers, body);
    }
    HttpResponse del(const std::string& url,
                     const std::vector<std::string>& headers = std::vector<std::string>()) const {
        return request("DELETE", url, headers, std::string());
    }
    static std::string form_escape(const std::string& s);
};
}
