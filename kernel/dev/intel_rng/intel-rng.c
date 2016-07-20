#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/x86/feature.h>
#include <dev/hw_rng.h>

static bool rdrand64(uint64_t *out) {
    /* TODO(security): RDRAND by itself is not suitable as a seed.
     * If we want to fallback to it, we should do further processing,
     * per Intel's recommendations */
    bool ret = false;
    __asm__ volatile ("xor %%al, %%al\r\n"
                      "rdrand %0\r\n"
                      "adc $0, %%al\r\n"
                      : "=r"(*out), "=a"(ret)
                      :
                      : "cc");
    return ret;
}

static bool rdseed64(uint64_t *out) {
    bool ret = false;
    __asm__ volatile ("xor %%al, %%al\r\n"
                      "rdseed %0\r\n"
                      "adc $0, %%al\r\n"
                      : "=r"(*out), "=a"(ret)
                      :
                      : "cc");
    return ret;
}

typedef bool (*hw_rnd_func)(uint64_t *out);

size_t hw_rng_get_entropy(void* buf, size_t len, bool block)
{
    if (!len) {
        return 0;
    }

    hw_rnd_func rnd = NULL;
    if (x86_feature_test(X86_FEATURE_RDSEED)) {
        rnd = rdseed64;
    } else if (x86_feature_test(X86_FEATURE_RDRAND)) {
        rnd = rdrand64;
    } else {
        /* We don't have an entropy source */
        return 0;
    }

    size_t written = 0;
    while (written < len) {
        uint64_t val;
        if (!rnd(&val)) {
            if (!block) {
                return written;
            }
            continue;
        }
        size_t to_copy = (len < sizeof(val)) ? len : sizeof(val);
        memcpy(buf, &val, to_copy);
        written += to_copy;
    }
    return written;
}
