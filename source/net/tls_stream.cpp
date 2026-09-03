#include "opennow/tls_stream.hpp"
#include "opennow/logger.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(OPENNOW_XDK)
#include <mbedtls/error.h>
#ifndef MBEDTLS_ERR_NET_SEND_FAILED
#define MBEDTLS_ERR_NET_SEND_FAILED -0x004E
#define MBEDTLS_ERR_NET_RECV_FAILED -0x004C
#endif
#endif

namespace opennow {

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl out;
    std::string::size_type sep = url.find("://");
    if (sep == std::string::npos) return out;
    out.scheme = url.substr(0, sep);
    std::string::size_type host_start = sep + 3;
    std::string::size_type slash = url.find('/', host_start);
    std::string::size_type query = url.find('?', host_start);
    std::string::size_type path_start = slash;
    if (path_start == std::string::npos || (query != std::string::npos && query < path_start)) path_start = query;
    std::string authority = path_start == std::string::npos
        ? url.substr(host_start) : url.substr(host_start, path_start - host_start);
    if (path_start == std::string::npos) out.path = "/";
    else if (url[path_start] == '?') out.path = "/" + url.substr(path_start);
    else out.path = url.substr(path_start);
    if (authority.empty()) return out;

    if (authority[0] == '[') {
        std::string::size_type end = authority.find(']');
        if (end == std::string::npos) return out;
        out.host = authority.substr(1, end - 1);
        if (end + 1 < authority.size() && authority[end + 1] == ':')
            out.port = std::atoi(authority.c_str() + end + 2);
    } else {
        std::string::size_type colon = authority.rfind(':');
        if (colon != std::string::npos && authority.find(':') == colon) {
            out.host = authority.substr(0, colon);
            out.port = std::atoi(authority.c_str() + colon + 1);
        } else {
            out.host = authority;
        }
    }
    if (out.port <= 0) out.port = (out.scheme == "https" || out.scheme == "wss") ? 443 : 80;
    out.valid = !out.host.empty();
    return out;
}

#if defined(OPENNOW_XDK)
TlsStream::TlsStream() : socket_(INVALID_SOCKET), socket_open_(false), tls_ready_(false) {
    mbedtls_ssl_init(&ssl_);
    mbedtls_ssl_config_init(&conf_);
    mbedtls_x509_crt_init(&ca_);
    mbedtls_entropy_init(&entropy_);
    mbedtls_ctr_drbg_init(&ctr_drbg_);
}

TlsStream::~TlsStream() { close(); }

void TlsStream::set_mbed_error(const char* where, int rc) {
    char full[256];
    #ifdef OPENNOW_XDK
    _snprintf(full, sizeof(full) - 1, "%s: -0x%04x", where, (unsigned)(-rc));
    full[sizeof(full)-1] = '\0';
#else
    std::snprintf(full, sizeof(full), "%s: -0x%04x", where, (unsigned)(-rc));
#endif
    error_ = full;
}

bool TlsStream::load_ca(const char* path) {
    ON_LOGD("tls", "CA load begin path=%s", path ? path : "<null>");
    if (!path || !*path) { error_ = "CA bundle path is empty"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { error_ = std::string("unable to open CA bundle: ") + path; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0 || n > 2 * 1024 * 1024) { error_ = "invalid CA bundle size"; return false; }
    std::vector<unsigned char> data((std::size_t)n + 1, 0);
    f.read(reinterpret_cast<char*>(&data[0]), n);
    if (!f) { error_ = "failed reading CA bundle"; return false; }
    int rc = mbedtls_x509_crt_parse(&ca_, &data[0], data.size());
    if (rc < 0) { set_mbed_error("mbedtls_x509_crt_parse", rc); ON_LOGE("tls", "CA parse failed bytes=%u rc=%d error=%s", (unsigned)data.size(), rc, error_.c_str()); return false; }
    ON_LOGI("tls", "CA load complete path=%s bytes=%u skipped_certificates=%d", path, (unsigned)n, rc);
    return true;
}

int TlsStream::bio_send(void* ctx, const unsigned char* buf, size_t len) {
    TlsStream* self = static_cast<TlsStream*>(ctx);
    int rc = ::send(self->socket_, reinterpret_cast<const char*>(buf), (int)len, 0);
    if (rc >= 0) return rc;
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    if (e == WSAEINTR) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

int TlsStream::bio_recv(void* ctx, unsigned char* buf, size_t len) {
    TlsStream* self = static_cast<TlsStream*>(ctx);
    int rc = ::recv(self->socket_, reinterpret_cast<char*>(buf), (int)len, 0);
    if (rc > 0) return rc;
    if (rc == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    if (e == WSAEINTR) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

bool TlsStream::connect(const std::string& host, int port, const char* ca_path) {
    close();
    error_.clear();
    const DWORD connect_started = GetTickCount();
    ON_LOGI("tls", "connect begin host=%s port=%d ca=%s", host.c_str(), port, ca_path ? ca_path : "<null>");
    ON_LOGI("socket", "preflight mode=XNet bypass-security plus DashLaunch sockpatch");

    IN_ADDR address;
    address.s_addr = inet_addr(host.c_str());
    if (address.s_addr == INADDR_NONE) {
        XNDNS* dns = NULL;
        int dns_rc = XNetDnsLookup(host.c_str(), NULL, &dns);
        if (dns_rc != 0 || !dns) { error_ = "DNS lookup failed"; ON_LOGE("dns", "lookup submit failed host=%s rc=%d ptr=%p wsa=%d", host.c_str(), dns_rc, dns, WSAGetLastError()); return false; }
        DWORD dns_started = GetTickCount();
        while (dns->iStatus == WSAEINPROGRESS && GetTickCount() - dns_started < 15000) Sleep(1);
        ON_LOGI("dns", "lookup complete host=%s status=%d addresses=%u elapsed_ms=%u", host.c_str(), dns->iStatus, dns->cina, (unsigned)(GetTickCount()-dns_started));
        if (dns->iStatus == 0 && dns->cina > 0) address = dns->aina[0];
        else address.s_addr = INADDR_NONE;
        XNetDnsRelease(dns);
        if (address.s_addr == INADDR_NONE) { error_ = "DNS lookup failed"; ON_LOGE("dns", "lookup yielded no usable IPv4 address host=%s", host.c_str()); return false; }
    }
    const unsigned char* ip = (const unsigned char*)&address.s_addr;
    ON_LOGI("socket", "resolved endpoint host=%s ipv4=%u.%u.%u.%u port=%d raw=0x%08x", host.c_str(), ip[0], ip[1], ip[2], ip[3], port, (unsigned)address.s_addr);
    sockaddr_in endpoint;
    ZeroMemory(&endpoint, sizeof(endpoint));
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons((u_short)port);
    endpoint.sin_addr = address;
    int last_socket_error = 0;
    /* Retail-kernel homebrew environments disagree about which undocumented
       mark-insecure option is exposed.  Probe each known form on a fresh
       socket and keep the first one that actually connects.  In particular,
       do not discard a socket merely because an optional flag was rejected:
       DashLaunch sockpatch can grant the privilege globally. */
    const int strategies[] = { 1, 2, 3, 0 }; /* 5801, 5802, both, global patch */
    const char* strategy_names[] = { "5801-only", "5802-only", "5802+5801", "global-only" };
    for (int attempt = 0; attempt < 4 && !socket_open_; ++attempt) {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        int create_error = socket_ == INVALID_SOCKET ? WSAGetLastError() : 0;
        ON_LOGD("socket", "probe attempt=%d strategy=%s create=0x%08x wsa=%d",
                attempt + 1, strategy_names[attempt], (unsigned)socket_, create_error);
        if (socket_ == INVALID_SOCKET) { last_socket_error = create_error; continue; }
        BOOL enabled = TRUE;
        int opt5801 = 0, err5801 = 0, opt5802 = 0, err5802 = 0;
        if (strategies[attempt] & 2) {
            opt5802 = setsockopt(socket_, SOL_SOCKET, 0x5802, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            if (opt5802 != 0) err5802 = WSAGetLastError();
        }
        if (strategies[attempt] & 1) {
            opt5801 = setsockopt(socket_, SOL_SOCKET, 0x5801, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
            if (opt5801 != 0) err5801 = WSAGetLastError();
        }
        int connect_rc = ::connect(socket_, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint));
        int connect_error = connect_rc == 0 ? 0 : WSAGetLastError();
        last_socket_error = connect_error;
        ON_LOGI("socket", "probe result strategy=%s opt5802=%d/%d opt5801=%d/%d connect=%d/%d elapsed_ms=%u",
                strategy_names[attempt], opt5802, err5802, opt5801, err5801,
                connect_rc, connect_error, (unsigned)(GetTickCount()-connect_started));
        if (connect_rc == 0) socket_open_ = true;
        else { closesocket(socket_); socket_ = INVALID_SOCKET; }
    }
    if (!socket_open_) { if(socket_==INVALID_SOCKET && !last_socket_error)last_socket_error=WSAGetLastError(); char b[96]; _snprintf(b,sizeof(b)-1,"TCP connect failed wsa=%d",last_socket_error); b[sizeof(b)-1]=0; error_=b; ON_LOGE("socket", "%s host=%s port=%d", error_.c_str(), host.c_str(), port); return false; }

    if (!load_ca(ca_path)) { close(); return false; }
    const char* pers = "opennow-xex";
    int rc = mbedtls_ctr_drbg_seed(&ctr_drbg_, mbedtls_entropy_func, &entropy_,
                                   reinterpret_cast<const unsigned char*>(pers), std::strlen(pers));
    if (rc != 0) { set_mbed_error("ctr_drbg_seed", rc); close(); return false; }
    rc = mbedtls_ssl_config_defaults(&conf_, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) { set_mbed_error("ssl_config_defaults", rc); close(); return false; }
    mbedtls_ssl_conf_rng(&conf_, mbedtls_ctr_drbg_random, &ctr_drbg_);
    mbedtls_ssl_conf_authmode(&conf_, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf_, &ca_, NULL);
    rc = mbedtls_ssl_setup(&ssl_, &conf_);
    if (rc != 0) { set_mbed_error("ssl_setup", rc); close(); return false; }
    rc = mbedtls_ssl_set_hostname(&ssl_, host.c_str());
    if (rc != 0) { set_mbed_error("ssl_set_hostname", rc); close(); return false; }
    mbedtls_ssl_set_bio(&ssl_, this, bio_send, bio_recv, NULL);
    ON_LOGI("tls", "handshake begin host=%s", host.c_str());
    unsigned handshake_spins=0;
    for (;;) {
        rc = mbedtls_ssl_handshake(&ssl_);
        if (rc == 0) break;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) { ++handshake_spins; continue; }
        set_mbed_error("ssl_handshake", rc); ON_LOGE("tls", "handshake failed host=%s rc=%d error=%s spins=%u elapsed_ms=%u", host.c_str(), rc, error_.c_str(),handshake_spins,(unsigned)(GetTickCount()-connect_started)); close(); return false;
    }
    uint32_t verify = mbedtls_ssl_get_verify_result(&ssl_);
    if (verify != 0) { error_ = "TLS certificate verification failed"; ON_LOGE("tls", "certificate verify failed host=%s flags=0x%08x",host.c_str(),(unsigned)verify); close(); return false; }
    tls_ready_ = true;
    ON_LOGI("tls", "connect complete host=%s port=%d spins=%u elapsed_ms=%u",host.c_str(),port,handshake_spins,(unsigned)(GetTickCount()-connect_started));
    return true;
}

bool TlsStream::set_nonblocking(bool enabled) {
    if (!socket_open_) return false;
    u_long value = enabled ? 1 : 0;
    return ioctlsocket(socket_, FIONBIO, &value) == 0;
}

bool TlsStream::write_all(const void* data, std::size_t bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    while (bytes) {
        int rc = mbedtls_ssl_write(&ssl_, p, bytes > 0x7fffffffU ? 0x7fffffff : (int)bytes);
        if (rc > 0) { p += rc; bytes -= (std::size_t)rc; continue; }
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) { Sleep(1); continue; }
        set_mbed_error("ssl_write", rc); return false;
    }
    return true;
}

int TlsStream::read_some(void* data, std::size_t bytes) {
    int rc = mbedtls_ssl_read(&ssl_, static_cast<unsigned char*>(data),
                              bytes > 0x7fffffffU ? 0x7fffffff : (int)bytes);
    if (rc > 0) return rc;
    if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
    if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) return -1;
    set_mbed_error("ssl_read", rc);
    return -2;
}

void TlsStream::close() {
    if (tls_ready_) mbedtls_ssl_close_notify(&ssl_);
    tls_ready_ = false;
    if (socket_open_) { closesocket(socket_); socket_open_ = false; }
    socket_ = INVALID_SOCKET;
    mbedtls_ssl_free(&ssl_); mbedtls_ssl_init(&ssl_);
    mbedtls_ssl_config_free(&conf_); mbedtls_ssl_config_init(&conf_);
    mbedtls_x509_crt_free(&ca_); mbedtls_x509_crt_init(&ca_);
    mbedtls_ctr_drbg_free(&ctr_drbg_); mbedtls_ctr_drbg_init(&ctr_drbg_);
    mbedtls_entropy_free(&entropy_); mbedtls_entropy_init(&entropy_);
}
#endif

} // namespace opennow
