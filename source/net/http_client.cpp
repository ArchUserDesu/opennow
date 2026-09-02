#include "opennow/http_client.hpp"
#include "opennow/ca_bundle.hpp"
#include "opennow/logger.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#if defined(OPENNOW_XDK)
#include "opennow/tls_stream.hpp"
#include <xtl.h>
#else
#include <chrono>
#include <curl/curl.h>
#endif

namespace opennow {
namespace {
std::string safe_url(std::string url) {
    std::string::size_type q = url.find('?');
    if (q != std::string::npos) url.resize(q);
    std::string::size_type s = url.find("/v2/session/");
    if (s != std::string::npos) { url.resize(s + 12); url += "<redacted>"; }
    return url;
}

#if defined(OPENNOW_XDK)
std::string lower_ascii(std::string s) {
    for (std::size_t i = 0; i < s.size(); ++i) s[i] = (char)std::tolower((unsigned char)s[i]);
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) --b;
    return s.substr(a, b-a);
}

bool decode_chunked(const std::string& in, std::string& out) {
    out.clear(); std::size_t pos = 0;
    while (pos < in.size()) {
        std::size_t eol = in.find("\r\n", pos);
        if (eol == std::string::npos) return false;
        std::string size_text = in.substr(pos, eol-pos);
        std::size_t semi = size_text.find(';'); if (semi != std::string::npos) size_text.resize(semi);
        char* end = NULL; unsigned long n = std::strtoul(size_text.c_str(), &end, 16);
        if (!end || end == size_text.c_str()) return false;
        pos = eol + 2;
        if (n == 0) return true;
        if (n > in.size() - pos) return false;
        out.append(in, pos, (std::size_t)n); pos += (std::size_t)n;
        if (pos + 2 > in.size() || in.compare(pos, 2, "\r\n") != 0) return false;
        pos += 2;
    }
    return false;
}

std::string resolve_redirect(const ParsedUrl& base, const std::string& location) {
    if (location.find("://") != std::string::npos) return location;
    std::ostringstream o; o << base.scheme << "://" << base.host;
    const bool default_port = (base.scheme == "https" && base.port == 443) || (base.scheme == "http" && base.port == 80);
    if (!default_port) o << ':' << base.port;
    if (!location.empty() && location[0] == '/') o << location;
    else {
        std::string parent = base.path;
        std::string::size_type slash = parent.rfind('/');
        if (slash == std::string::npos) parent = "/"; else parent.resize(slash + 1);
        o << parent << location;
    }
    return o.str();
}

HttpResponse xdk_request_once(const std::string& method, const std::string& url,
                              const std::vector<std::string>& headers, const std::string& body,
                              std::string* redirect) {
    HttpResponse out;
    ParsedUrl u = parse_url(url);
    if (!u.valid || u.scheme != "https") { out.error = "XEX HTTP transport requires https://"; return out; }
    TlsStream tls;
    if (!tls.connect(u.host, u.port, ca_bundle_path())) { out.error = tls.error(); return out; }

    std::ostringstream req;
    req << method << ' ' << u.path << " HTTP/1.1\r\n";
    req << "Host: " << u.host;
    if (u.port != 443) req << ':' << u.port;
    req << "\r\n";
    req << "Connection: close\r\n";
    req << "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/128.0.0.0 Safari/537.36 NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173\r\n";
    bool have_len = false;
    for (std::size_t i = 0; i < headers.size(); ++i) {
        std::string low = lower_ascii(headers[i]);
        if (low.find("host:") == 0 || low.find("connection:") == 0 || low.find("user-agent:") == 0) continue;
        if (low.find("content-length:") == 0) have_len = true;
        req << headers[i] << "\r\n";
    }
    if (!body.empty() && !have_len) req << "Content-Length: " << body.size() << "\r\n";
    req << "\r\n";
    std::string wire = req.str(); wire += body;
    if (!tls.write_all(wire.c_str(), wire.size())) { out.error = tls.error(); return out; }

    std::string response; char buf[8192];
    for (;;) {
        int n = tls.read_some(buf, sizeof(buf));
        if (n > 0) { response.append(buf, n); if (response.size() > 16 * 1024 * 1024) { out.error = "HTTP response too large"; return out; } continue; }
        if (n == 0) break;
        if (n == -1) { Sleep(1); continue; }
        out.error = tls.error(); return out;
    }
    std::size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) { out.error = "malformed HTTP response"; return out; }
    std::string head = response.substr(0, header_end);
    std::string payload = response.substr(header_end + 4);
    std::size_t first_eol = head.find("\r\n");
    std::string status = first_eol == std::string::npos ? head : head.substr(0, first_eol);
    std::size_t sp = status.find(' ');
    if (sp != std::string::npos) out.status_code = std::strtol(status.c_str() + sp + 1, NULL, 10);

    bool chunked = false;
    std::size_t pos = first_eol == std::string::npos ? head.size() : first_eol + 2;
    while (pos < head.size()) {
        std::size_t e = head.find("\r\n", pos); if (e == std::string::npos) e = head.size();
        std::string line = head.substr(pos, e-pos); std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = lower_ascii(trim(line.substr(0, colon)));
            std::string value = trim(line.substr(colon + 1));
            std::string lvalue = lower_ascii(value);
            if (name == "transfer-encoding" && lvalue.find("chunked") != std::string::npos) chunked = true;
            if (name == "location" && redirect) *redirect = value;
        }
        pos = e + 2;
    }
    if (chunked) {
        if (!decode_chunked(payload, out.body)) { out.error = "invalid chunked HTTP response"; return out; }
    } else out.body.swap(payload);
    return out;
}
#else
size_t write_cb(char* ptr, size_t size, size_t nmemb, void* ud) {
    std::string* s = static_cast<std::string*>(ud); s->append(ptr, size * nmemb); return size * nmemb;
}
#endif
} // namespace

HttpClient::HttpClient() {
#if defined(OPENNOW_XDK)
    ON_LOGI("http", "XEX HTTPS transport initialized (mbedTLS/Winsock)");
#else
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    ON_LOGI("http", "curl_global_init rc=%d version=%s", (int)rc, curl_version());
#endif
}
HttpClient::~HttpClient() {}

HttpResponse HttpClient::request(const std::string& method, const std::string& url,
                                 const std::vector<std::string>& headers, const std::string& body) const {
#if defined(OPENNOW_XDK)
    DWORD started = GetTickCount();
    std::string current = url;
    std::string current_method = method;
    std::string current_body = body;
    for (int hop = 0; hop < 6; ++hop) {
        std::string redirect;
        HttpResponse out = xdk_request_once(current_method, current, headers, current_body, &redirect);
        if (!out.error.empty()) {
            ON_LOGE("http", "request failed method=%s url=%s error=%s elapsed_ms=%u",
                    method.c_str(), safe_url(current).c_str(), out.error.c_str(), (unsigned)(GetTickCount()-started));
            return out;
        }
        if ((out.status_code == 301 || out.status_code == 302 || out.status_code == 303 ||
             out.status_code == 307 || out.status_code == 308) && !redirect.empty()) {
            ParsedUrl base = parse_url(current); current = resolve_redirect(base, redirect);
            if (out.status_code == 303 || ((out.status_code == 301 || out.status_code == 302) && current_method == "POST")) {
                current_method = "GET"; current_body.clear();
            }
            continue;
        }
        ON_LOGI("http", "request end method=%s url=%s status=%ld response_bytes=%u elapsed_ms=%u",
                method.c_str(), safe_url(current).c_str(), out.status_code, (unsigned)out.body.size(),
                (unsigned)(GetTickCount()-started));
        return out;
    }
    HttpResponse out; out.error = "too many HTTP redirects"; return out;
#else
    const auto started = std::chrono::steady_clock::now();
    const std::string logged_url = safe_url(url);
    ON_LOGI("http", "request begin method=%s url=%s request_bytes=%u headers=%u", method.c_str(), logged_url.c_str(),
            (unsigned)body.size(), (unsigned)headers.size());
    HttpResponse out; CURL* c = curl_easy_init();
    if (!c) { out.error = "curl_easy_init failed"; return out; }
    curl_slist* hs = NULL; for (const auto& h : headers) hs = curl_slist_append(hs, h.c_str());
    curl_easy_setopt(c, CURLOPT_URL, url.c_str()); curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb); curl_easy_setopt(c, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/128.0.0.0 Safari/537.36 NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 12000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 30000L); curl_easy_setopt(c, CURLOPT_TCP_NODELAY, 1L);
    const char* ca = ca_bundle_path(); curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    if (method == "POST") { curl_easy_setopt(c, CURLOPT_POST, 1L); curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data()); curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size()); }
    else if (method != "GET") curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    CURLcode rc = curl_easy_perform(c); if (rc != CURLE_OK) out.error = curl_easy_strerror(rc);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &out.status_code); curl_slist_free_all(hs); curl_easy_cleanup(c);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    if (rc == CURLE_OK) ON_LOGI("http", "request end method=%s url=%s status=%ld response_bytes=%u elapsed_ms=%lld ca=%s", method.c_str(), logged_url.c_str(), out.status_code, (unsigned)out.body.size(), (long long)elapsed, ca);
    else ON_LOGE("http", "request failed method=%s url=%s curl_rc=%d error=%s status=%ld elapsed_ms=%lld ca=%s", method.c_str(), logged_url.c_str(), (int)rc, out.error.c_str(), out.status_code, (long long)elapsed, ca);
    return out;
#endif
}

std::string HttpClient::form_escape(const std::string& s) {
#if defined(OPENNOW_XDK)
    static const char hex[] = "0123456789ABCDEF";
    std::string out; out.reserve(s.size() * 3);
    for (std::size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
#else
    CURL* c = curl_easy_init(); if (!c) return s; char* e = curl_easy_escape(c, s.c_str(), (int)s.size());
    std::string r = e ? e : s; if (e) curl_free(e); curl_easy_cleanup(c); return r;
#endif
}
} // namespace opennow
