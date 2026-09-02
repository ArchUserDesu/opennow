#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if defined(OPENNOW_XDK)
#include <xtl.h>
#include <winsockx.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#endif

namespace opennow {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    std::string path;
    int port;
    bool valid;
    ParsedUrl() : port(0), valid(false) {}
};

ParsedUrl parse_url(const std::string& url);

#if defined(OPENNOW_XDK)
class TlsStream {
public:
    TlsStream();
    ~TlsStream();

    bool connect(const std::string& host, int port, const char* ca_path);
    void close();
    bool set_nonblocking(bool enabled);
    bool write_all(const void* data, std::size_t bytes);
    int read_some(void* data, std::size_t bytes); // >0 bytes, 0 closed, -1 would-block, -2 error
    const std::string& error() const { return error_; }

private:
    TlsStream(const TlsStream&);
    TlsStream& operator=(const TlsStream&);

    static int bio_send(void* ctx, const unsigned char* buf, size_t len);
    static int bio_recv(void* ctx, unsigned char* buf, size_t len);
    bool load_ca(const char* path);
    void set_mbed_error(const char* where, int rc);

    SOCKET socket_;
    bool socket_open_;
    bool tls_ready_;
    mbedtls_ssl_context ssl_;
    mbedtls_ssl_config conf_;
    mbedtls_x509_crt ca_;
    mbedtls_entropy_context entropy_;
    mbedtls_ctr_drbg_context ctr_drbg_;
    std::string error_;
};
#endif

} // namespace opennow
