#if defined(OPENNOW_XDK)

#include <xtl.h>
#include <mbedtls/entropy.h>

#include <stddef.h>
#include <time.h>

// Exported by xboxkrnl on Xbox 360. This keeps TLS/WebRTC entropy sourced from
// the console rather than falling back to rand()/time-based pseudo-randomness.
extern "C" void XeCryptRandom(BYTE* output, DWORD bytes);

extern "C" int mbedtls_hardware_poll(void* data, unsigned char* output,
                                      size_t len, size_t* olen) {
    (void)data;
    if (!output || !olen) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    const size_t requested = len;
    while (len) {
        const DWORD chunk = len > 0xffffffffu ? 0xffffffffu : (DWORD)len;
        XeCryptRandom(output, chunk);
        output += chunk;
        len -= chunk;
    }
    *olen = requested;
    return 0;
}

extern "C" int usleep(unsigned int usec) {
    // XDK Sleep is millisecond based. Round up so timeout/ICE code never wakes
    // earlier than requested.
    Sleep((usec + 999u) / 1000u);
    return 0;
}

extern "C" int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (!tv) return -1;
    const time_t now = time(NULL);
    tv->tv_sec = (long)now;
    tv->tv_usec = (long)((GetTickCount() % 1000u) * 1000u);
    return 0;
}

extern "C" long gethostid(void) {
    XNADDR addr;
    ZeroMemory(&addr, sizeof(addr));
    DWORD status = XNetGetTitleXnAddr(&addr);
    if (status == XNET_GET_XNADDR_NONE || status == XNET_GET_XNADDR_PENDING) return 0;
    return (long)addr.ina.s_addr;
}

#endif
