#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <arch/x86/feature.h>
#include <dev/hw_rng.h>

enum entropy_instr {
    ENTROPY_INSTR_RDSEED,
    ENTROPY_INSTR_RDRAND,
};
static ssize_t get_entropy_from_instruction(void* buf, size_t len, bool block,
                                            enum entropy_instr instr);
static ssize_t get_entropy_from_rdseed(void* buf, size_t len, bool block);
static ssize_t get_entropy_from_rdrand(void* buf, size_t len, bool block);

/* @brief Get entropy from the CPU using RDSEED.
 *
 * len must be at most SSIZE_MAX
 *
 * If |block|=true, it will retry the RDSEED instruction until |len| bytes are
 * written to |buf|.  Otherwise, it will fetch data from RDSEED until either
 * |len| bytes are written to |buf| or RDSEED is unable to return entropy.
 *
 * Returns the number of bytes written to the buffer on success (potentially 0),
 * and a negative value on error.
 */
__NO_SAFESTACK static ssize_t get_entropy_from_cpu(void* buf, size_t len,
                                                   bool block) {
    /* TODO(security): Move this to a shared kernel/user lib, so we can write usermode
     * tests against this code */

    if (len >= SSIZE_MAX) {
        static_assert(ERR_INVALID_ARGS < 0, "");
        return ERR_INVALID_ARGS;
    }

    if (x86_feature_test(X86_FEATURE_RDSEED)) {
        return get_entropy_from_rdseed(buf, len, block);
    } else if (x86_feature_test(X86_FEATURE_RDRAND)) {
        return get_entropy_from_rdrand(buf, len, block);
    }

    /* We don't have an entropy source */
    static_assert(ERR_NOT_SUPPORTED < 0, "");
    return ERR_NOT_SUPPORTED;
}

__NO_SAFESTACK static bool rdrand64_step(unsigned long long int* val) {
    bool success = false;
    __asm__ volatile ("rdrand %0; setc %1" : "=r"(*val), "=r"(success) : : "cc");
    return success;
}

__NO_SAFESTACK static bool rdseed64_step(unsigned long long int* val) {
    // We use inline asm here instead of the intrinsic since we observed a
    // codegen bug with rdseed in GCC 6.2.
    bool success = false;
    __asm__ volatile ("rdseed %0; setc %1" : "=r"(*val), "=r"(success) : : "cc");
    return success;
}

__NO_SAFESTACK static bool instruction_step(enum entropy_instr instr,
                                            unsigned long long int* val) {
    switch (instr) {
        case ENTROPY_INSTR_RDRAND: return rdrand64_step(val);
        case ENTROPY_INSTR_RDSEED: return rdseed64_step(val);
        default: panic("Invalid entropy instruction %u\n", instr);
    }
}

__NO_SAFESTACK static ssize_t get_entropy_from_instruction(
    void* buf, size_t len, bool block, enum entropy_instr instr) {
    size_t written = 0;
    while (written < len) {
        unsigned long long int val = 0;
        if (!instruction_step(instr, &val)) {
            if (!block) {
                break;
            }
            continue;
        }
        const size_t to_copy = (len < sizeof(val)) ? len : sizeof(val);
        memcpy(buf + written, &val, to_copy);
        written += to_copy;
    }
    if (block) {
        DEBUG_ASSERT(written == len);
    }
    return (ssize_t)written;
}

__NO_SAFESTACK static ssize_t get_entropy_from_rdseed(void* buf, size_t len,
                                                      bool block) {
    return get_entropy_from_instruction(buf, len, block, ENTROPY_INSTR_RDSEED);
}

__NO_SAFESTACK static ssize_t get_entropy_from_rdrand(void* buf, size_t len,
                                                      bool block) {
    // TODO(security): This method is not compliant with Intel's "Digital Random
    // Number Generator (DRNG) Software Implementation Guide".  We are using
    // rdrand in a way that is explicitly against their recommendations.  This
    // needs to be corrected, but this fallback is a compromise to allow our
    // development platforms that don't support RDSEED to get some degree of
    // hardware-based randomization.
    return get_entropy_from_instruction(buf, len, block, ENTROPY_INSTR_RDRAND);
}

__NO_SAFESTACK size_t hw_rng_get_entropy(void* buf, size_t len, bool block)
{
    if (!len) {
        return 0;
    }

    ssize_t res = get_entropy_from_cpu(buf, len, block);
    if (res < 0) {
        return 0;
    }
    return (size_t)res;
}
