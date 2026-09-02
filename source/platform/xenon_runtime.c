#include <mbedtls/sha256.h>
#include <mbedtls/timing.h>

#include <ppc/timebase.h>
#include <time/time.h>
#include <xb360/xb360.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

int usleep(unsigned int usec)
{
    udelay((int)usec);
    return 0;
}

static uint64_t timing_load(const struct mbedtls_timing_hr_time *timer)
{
    uint64_t start;
    memcpy(&start, timer->MBEDTLS_PRIVATE(opaque), sizeof(start));
    return start;
}

static void timing_store(struct mbedtls_timing_hr_time *timer, uint64_t start)
{
    memset(timer->MBEDTLS_PRIVATE(opaque), 0, sizeof(timer->MBEDTLS_PRIVATE(opaque)));
    memcpy(timer->MBEDTLS_PRIVATE(opaque), &start, sizeof(start));
}

unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *timer, int reset)
{
    uint64_t now = mftb();
    uint64_t start = timing_load(timer);
    if (reset || start == 0) {
        timing_store(timer, now);
        return 0;
    }
    return tb_diff_msec(now, start);
}

void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms)
{
    mbedtls_timing_delay_context *ctx = data;
    ctx->MBEDTLS_PRIVATE(int_ms) = int_ms;
    ctx->MBEDTLS_PRIVATE(fin_ms) = fin_ms;
    if (fin_ms != 0) mbedtls_timing_get_timer(&ctx->MBEDTLS_PRIVATE(timer), 1);
}

int mbedtls_timing_get_delay(void *data)
{
    mbedtls_timing_delay_context *ctx = data;
    unsigned long elapsed;
    if (ctx->MBEDTLS_PRIVATE(fin_ms) == 0) return -1;
    elapsed = mbedtls_timing_get_timer(&ctx->MBEDTLS_PRIVATE(timer), 0);
    if (elapsed >= ctx->MBEDTLS_PRIVATE(fin_ms)) return 2;
    if (elapsed >= ctx->MBEDTLS_PRIVATE(int_ms)) return 1;
    return 0;
}

uint32_t mbedtls_timing_get_final_delay(const mbedtls_timing_delay_context *data)
{
    return data->MBEDTLS_PRIVATE(fin_ms);
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    static volatile uint64_t sequence;
    unsigned char cpu_key[16];
    unsigned char material[48];
    unsigned char digest[32];
    size_t produced = 0;
    uint64_t now;
    uint64_t serial;
    (void)data;
    if (!output || !olen) return -1;
    if (cpu_get_key(cpu_key) != 0) return -1;

    while (produced < len) {
        size_t amount;
        now = mftb();
        serial = __sync_add_and_fetch(&sequence, 1);
        memset(material, 0, sizeof(material));
        memcpy(material, cpu_key, sizeof(cpu_key));
        memcpy(material + 16, &now, sizeof(now));
        memcpy(material + 24, &serial, sizeof(serial));
        memcpy(material + 32, &output, sizeof(output));
        memcpy(material + 40, &len, sizeof(len));
        if (mbedtls_sha256(material, sizeof(material), digest, 0) != 0) {
            memset(cpu_key, 0, sizeof(cpu_key));
            return -1;
        }
        amount = len - produced;
        if (amount > sizeof(digest)) amount = sizeof(digest);
        memcpy(output + produced, digest, amount);
        produced += amount;
    }
    memset(cpu_key, 0, sizeof(cpu_key));
    memset(material, 0, sizeof(material));
    memset(digest, 0, sizeof(digest));
    *olen = len;
    return 0;
}
