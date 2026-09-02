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
    char detail[160];
    detail[0] = 0;
    mbedtls_strerror(rc, detail, sizeof(detail));
    char full[256];
    #ifdef OPENNOW_XDK
    _snprintf(full, sizeof(full) - 1, "%s: -0x%04x %s", where, (unsigned)(-rc), detail);
    full[sizeof(full)-1] = '\0';
#else
    std::snprintf(full, sizeof(full), "%s: -0x%04x %s", where, (unsigned)(-rc), detail);
#endif
    error_ = full;
}

bool TlsStream::load_ca(const char* path) {
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
    if (rc < 0) { set_mbed_error("mbedtls_x509_crt_parse", rc); return false; }
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

    char port_text[16];
    _snprintf(port_text, sizeof(port_text) - 1, "%d", port); port_text[sizeof(port_text)-1] = '\0';
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // Xbox 360 title stack is most reliable over IPv4.
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* result = NULL;
    int gai = getaddrinfo(host.c_str(), port_text, &hints, &result);
    if (gai != 0 || !result) {
        char msg[128]; _snprintf(msg, sizeof(msg) - 1, "getaddrinfo failed: %d", gai); msg[sizeof(msg)-1] = '\0'; error_ = msg; return false;
    }
    for (struct addrinfo* ai = result; ai; ai = ai->ai_next) {
        socket_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (socket_ == INVALID_SOCKET) continue;
        BOOL bypass = TRUE;
        // Retail homebrew titles generally need these XNet socket options for ordinary Internet traffic.
        setsockopt(socket_, SOL_SOCKET, 0x5801, reinterpret_cast<const char*>(&bypass), sizeof(bypass));
        setsockopt(socket_, SOL_SOCKET, 0x5802, reinterpret_cast<const char*>(&bypass), sizeof(bypass));
        if (::connect(socket_, ai->ai_addr, (int)ai->ai_addrlen) == 0) { socket_open_ = true; break; }
        closesocket(socket_); socket_ = INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (!socket_open_) { error_ = "TCP connect failed"; return false; }

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
    for (;;) {
        rc = mbedtls_ssl_handshake(&ssl_);
        if (rc == 0) break;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        set_mbed_error("ssl_handshake", rc); close(); return false;
    }
    uint32_t verify = mbedtls_ssl_get_verify_result(&ssl_);
    if (verify != 0) { error_ = "TLS certificate verification failed"; close(); return false; }
    tls_ready_ = true;
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
