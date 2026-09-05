#if defined(OPENNOW_XDK)

#include <xtl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/timing.h>

#include <stddef.h>
#include <string.h>
#include <time.h>

// Exported by xboxkrnl on Xbox 360. This keeps TLS/WebRTC entropy sourced from
// the console rather than falling back to rand()/time-based pseudo-randomness.
extern "C" void XeCryptRandom(BYTE* output, DWORD bytes);

static DWORD timing_load(const mbedtls_timing_hr_time* timer) {
    DWORD started = 0;
    memcpy(&started, timer->MBEDTLS_PRIVATE(opaque), sizeof(started));
    return started;
}

extern "C" unsigned long mbedtls_timing_get_timer(mbedtls_timing_hr_time* timer, int reset) {
    const DWORD now = GetTickCount();
    DWORD started = timing_load(timer);
    if (reset || started == 0) {
        memset(timer->MBEDTLS_PRIVATE(opaque), 0, sizeof(timer->MBEDTLS_PRIVATE(opaque)));
        memcpy(timer->MBEDTLS_PRIVATE(opaque), &now, sizeof(now));
        return 0;
    }
    return (unsigned long)(now - started);
}

extern "C" void mbedtls_timing_set_delay(void* data, uint32_t int_ms, uint32_t fin_ms) {
    mbedtls_timing_delay_context* ctx = (mbedtls_timing_delay_context*)data;
    ctx->MBEDTLS_PRIVATE(int_ms) = int_ms;
    ctx->MBEDTLS_PRIVATE(fin_ms) = fin_ms;
    if (fin_ms != 0) mbedtls_timing_get_timer(&ctx->MBEDTLS_PRIVATE(timer), 1);
}

extern "C" int mbedtls_timing_get_delay(void* data) {
    mbedtls_timing_delay_context* ctx = (mbedtls_timing_delay_context*)data;
    if (ctx->MBEDTLS_PRIVATE(fin_ms) == 0) return -1;
    const unsigned long elapsed = mbedtls_timing_get_timer(&ctx->MBEDTLS_PRIVATE(timer), 0);
    if (elapsed >= ctx->MBEDTLS_PRIVATE(fin_ms)) return 2;
    if (elapsed >= ctx->MBEDTLS_PRIVATE(int_ms)) return 1;
    return 0;
}

extern "C" uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context* data) {
    return data->MBEDTLS_PRIVATE(fin_ms);
}

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
    // Both fields must come from the same clock sample. Combining time()
    // seconds with uptime's remainder jumps backwards at every uptime second.
    FILETIME ft;
    ULARGE_INTEGER ticks;
    GetSystemTimeAsFileTime(&ft);
    ticks.LowPart=ft.dwLowDateTime;ticks.HighPart=ft.dwHighDateTime;
    const unsigned __int64 unix_us=(ticks.QuadPart-116444736000000000ULL)/10ULL;
    tv->tv_sec=(long)(unix_us/1000000ULL);
    tv->tv_usec=(long)(unix_us%1000000ULL);
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
