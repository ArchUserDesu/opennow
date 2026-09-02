#include "opennow/logger.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>

#ifdef __LIBXENON__
extern "C" {
#include <ppc/timebase.h>
#include <time/time.h>
}
#elif defined(OPENNOW_XDK)
#include <xtl.h>
#endif

namespace opennow {
namespace {

std::FILE* g_file = NULL;
const char* g_path = "unavailable";
#if defined(OPENNOW_XDK)
CRITICAL_SECTION g_lock;
volatile LONG g_lock_ready = 0;
#else
volatile unsigned int g_lock = 0;
#endif
unsigned long long g_sequence = 0;
#ifdef __LIBXENON__
std::uint64_t g_started = 0;
#elif defined(OPENNOW_XDK)
DWORD g_started = 0;
#endif

const char* level_name(LogLevel level) {
    switch (level) {
        case LogTrace: return "TRACE";
        case LogDebug: return "DEBUG";
        case LogInfo: return "INFO ";
        case LogWarn: return "WARN ";
        case LogError: return "ERROR";
        case LogFatal: return "FATAL";
    }
    return "?????";
}

void lock_log() {
#if defined(OPENNOW_XDK)
    if (InterlockedCompareExchange(&g_lock_ready, 1, 0) == 0) {
        InitializeCriticalSection(&g_lock);
        InterlockedExchange(&g_lock_ready, 2);
    } else {
        while (InterlockedCompareExchange(&g_lock_ready, 2, 2) != 2) Sleep(0);
    }
    EnterCriticalSection(&g_lock);
#else
    while (__sync_lock_test_and_set(&g_lock, 1U)) {
        while (g_lock) {}
    }
#endif
}

void unlock_log() {
#if defined(OPENNOW_XDK)
    LeaveCriticalSection(&g_lock);
#else
    __sync_lock_release(&g_lock);
#endif
}

unsigned long elapsed_ms() {
#ifdef __LIBXENON__
    return g_started ? tb_diff_msec(mftb(), g_started) : 0;
#elif defined(OPENNOW_XDK)
    return g_started ? (unsigned long)(GetTickCount() - g_started) : 0;
#else
    return 0;
#endif
}

} // namespace

bool log_init() {
    lock_log();
    if (g_file) {
        unlock_log();
        return true;
    }
#ifdef __LIBXENON__
    static const char* const candidates[] = {
        "uda:/opennow.log", "sda:/opennow.log", "opennow.log"
    };
    g_started = mftb();
#elif defined(OPENNOW_XDK)
    static const char* const candidates[] = {
        "game:\\opennow.log", "opennow.log"
    };
    g_started = GetTickCount();
#else
    static const char* const candidates[] = {"opennow.log"};
#endif
    for (std::size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        const char* candidate = candidates[i];
        g_file = std::fopen(candidate, "wb");
        if (g_file) {
            g_path = candidate;
            break;
        }
    }
    const bool opened = g_file != NULL;
    if (opened) {
        std::setvbuf(g_file, NULL, _IOLBF, 0);
        std::fprintf(g_file, "OpenNOW-Xenon persistent diagnostic log\n");
        std::fprintf(g_file, "Log format: sequence elapsed-ms level component message\n");
        std::fprintf(g_file, "Secrets, authorization headers, and request bodies are intentionally omitted.\n");
        std::fflush(g_file);
    }
    unlock_log();
    if (opened) {
        log_message(LogInfo, "logger", "persistent logging opened at %s", g_path);
    } else {
        std::printf("OpenNOW-Xenon: could not create opennow.log\n");
    }
    return opened;
}

void log_close() {
    lock_log();
    if (g_file) {
        std::fprintf(g_file, "%06llu +%010lums INFO  logger: closing persistent log\n",
                     ++g_sequence, elapsed_ms());
        std::fflush(g_file);
        std::fclose(g_file);
        g_file = NULL;
    }
    unlock_log();
}

const char* log_path() {
    return g_path;
}

void log_message(LogLevel level, const char* component, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    #ifdef OPENNOW_XDK
    _vsnprintf(message, sizeof(message) - 1, format, args);
    message[sizeof(message)-1] = '\0';
#else
    std::vsnprintf(message, sizeof(message), format, args);
#endif
    va_end(args);
    message[sizeof(message) - 1] = '\0';

    lock_log();
    const unsigned long long sequence = ++g_sequence;
    const unsigned long elapsed = elapsed_ms();
    std::printf("[%06llu +%lums %s %s] %s\n", sequence, elapsed,
                level_name(level), component ? component : "general", message);
    if (g_file) {
        std::fprintf(g_file, "%06llu +%010lums %s %s: %s\n", sequence, elapsed,
                     level_name(level), component ? component : "general", message);
        std::fflush(g_file);
    }
    unlock_log();
}

} // namespace opennow
