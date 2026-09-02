#include <stdint.h>
#include <time.h>
#include <sys/times.h>

#include <ppc/timebase.h>

/*
 * Newlib clock() ultimately expects the POSIX times() syscall.
 *
 * LibXenon does not currently provide times(), but Xenon exposes a
 * hardware PowerPC time-base counter via mftb().
 *
 * Convert that monotonically increasing hardware counter into
 * CLOCKS_PER_SEC units.
 */
clock_t times(struct tms *buffer)
{
    const uint64_t tb =
        mftb();

    /*
     * Avoid:
     *
     *     tb * CLOCKS_PER_SEC
     *
     * directly, since a long-running system could overflow uint64_t.
     */
    const uint64_t seconds =
        tb / (uint64_t)PPC_TIMEBASE_FREQ;

    const uint64_t remainder =
        tb % (uint64_t)PPC_TIMEBASE_FREQ;

    const uint64_t ticks64 =
        seconds * (uint64_t)CLOCKS_PER_SEC +
        (
            remainder * (uint64_t)CLOCKS_PER_SEC
        ) / (uint64_t)PPC_TIMEBASE_FREQ;

    const clock_t ticks =
        (clock_t)ticks64;

    if (buffer != 0) {
        /*
         * Bare-metal OpenNOW is effectively one process.
         *
         * Treat elapsed Xenon time as user time. This gives Newlib
         * clock() a useful monotonic value while keeping the other
         * POSIX process accounting fields zero.
         */
        buffer->tms_utime  = ticks;
        buffer->tms_stime  = 0;
        buffer->tms_cutime = 0;
        buffer->tms_cstime = 0;
    }

    return ticks;
}
