// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <magenta/compiler.h>
#include <stdint.h>
#include <arch/x86.h>

__BEGIN_CDECLS

#define MAX_SUPPORTED_CPUID     (0x17)
#define MAX_SUPPORTED_CPUID_EXT (0x8000001e)

struct cpuid_leaf {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
};

enum x86_cpuid_leaf_num {
    X86_CPUID_BASE = 0,
    X86_CPUID_MODEL_FEATURES = 0x1,
    X86_CPUID_CACHE_V1 = 0x2,
    X86_CPUID_CACHE_V2 = 0x4,
    X86_CPUID_THERMAL_AND_POWER = 0x6,
    X86_CPUID_EXTENDED_FEATURE_FLAGS = 0x7,
    X86_CPUID_PERFORMANCE_MONITORING = 0xa,
    X86_CPUID_TOPOLOGY = 0xb,
    X86_CPUID_XSAVE = 0xd,
    X86_CPUID_PT = 0x14,
    X86_CPUID_TSC = 0x15,

    X86_CPUID_EXT_BASE = 0x80000000,
    X86_CPUID_ADDR_WIDTH = 0x80000008,
    X86_CPUID_AMD_TOPOLOGY = 0x8000001e,
};

struct x86_cpuid_bit {
    enum x86_cpuid_leaf_num leaf_num;
    uint8_t word;
    uint8_t bit;
};

#define X86_CPUID_BIT(leaf, word, bit) \
        (struct x86_cpuid_bit){(enum x86_cpuid_leaf_num)(leaf), (word), (bit)}

void x86_feature_init(void);

static inline const struct cpuid_leaf *x86_get_cpuid_leaf(enum x86_cpuid_leaf_num leaf)
{
    extern struct cpuid_leaf _cpuid[MAX_SUPPORTED_CPUID + 1];
    extern struct cpuid_leaf _cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
    extern uint32_t max_cpuid;
    extern uint32_t max_ext_cpuid;

    if (leaf < X86_CPUID_EXT_BASE) {
        if (leaf > max_cpuid)
            return NULL;

        return &_cpuid[leaf];
    } else {
        if (leaf > max_ext_cpuid)
            return NULL;

        return &_cpuid_ext[(uint32_t)leaf - (uint32_t)X86_CPUID_EXT_BASE];
    }
}
/* Retrieve the specified subleaf.  This function is not cached.
 * Returns false if leaf num is invalid */
bool x86_get_cpuid_subleaf(
        enum x86_cpuid_leaf_num, uint32_t, struct cpuid_leaf *);

static inline bool x86_feature_test(struct x86_cpuid_bit bit)
{
    DEBUG_ASSERT (bit.word <= 3 && bit.bit <= 31);

    if (bit.word > 3 || bit.bit > 31)
        return false;

    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(bit.leaf_num);
    if (!leaf)
        return false;

    switch (bit.word) {
        case 0: return !!((1u << bit.bit) & leaf->a);
        case 1: return !!((1u << bit.bit) & leaf->b);
        case 2: return !!((1u << bit.bit) & leaf->c);
        case 3: return !!((1u << bit.bit) & leaf->d);
        default: return false;
    }
}

void x86_feature_debug(void);

/* add feature bits to test here */
#define X86_FEATURE_SSE3         X86_CPUID_BIT(0x1, 2, 0)
#define X86_FEATURE_VMX          X86_CPUID_BIT(0x1, 2, 5)
#define X86_FEATURE_SSSE3        X86_CPUID_BIT(0x1, 2, 9)
#define X86_FEATURE_PDCM         X86_CPUID_BIT(0x1, 2, 15)
#define X86_FEATURE_SSE4_1       X86_CPUID_BIT(0x1, 2, 19)
#define X86_FEATURE_SSE4_2       X86_CPUID_BIT(0x1, 2, 20)
#define X86_FEATURE_X2APIC       X86_CPUID_BIT(0x1, 2, 21)
#define X86_FEATURE_TSC_DEADLINE X86_CPUID_BIT(0x1, 2, 24)
#define X86_FEATURE_AESNI        X86_CPUID_BIT(0x1, 2, 25)
#define X86_FEATURE_XSAVE        X86_CPUID_BIT(0x1, 2, 26)
#define X86_FEATURE_AVX          X86_CPUID_BIT(0x1, 2, 28)
#define X86_FEATURE_RDRAND       X86_CPUID_BIT(0x1, 2, 30)
#define X86_FEATURE_HYPERVISOR   X86_CPUID_BIT(0x1, 2, 31)
#define X86_FEATURE_FPU          X86_CPUID_BIT(0x1, 3, 0)
#define X86_FEATURE_MMX          X86_CPUID_BIT(0x1, 3, 23)
#define X86_FEATURE_FXSR         X86_CPUID_BIT(0x1, 3, 24)
#define X86_FEATURE_SSE          X86_CPUID_BIT(0x1, 3, 25)
#define X86_FEATURE_SSE2         X86_CPUID_BIT(0x1, 3, 26)
#define X86_FEATURE_TM           X86_CPUID_BIT(0x1, 3, 29)
#define X86_FEATURE_HWP          X86_CPUID_BIT(0x6, 0, 7)
#define X86_FEATURE_HWP_PREF     X86_CPUID_BIT(0x6, 0, 10)
#define X86_FEATURE_HW_FEEDBACK  X86_CPUID_BIT(0x6, 2, 0)
#define X86_FEATURE_PERF_BIAS    X86_CPUID_BIT(0x6, 2, 3)
#define X86_FEATURE_FSGSBASE     X86_CPUID_BIT(0x7, 1, 0)
#define X86_FEATURE_TSC_ADJUST   X86_CPUID_BIT(0x7, 1, 1)
#define X86_FEATURE_AVX2         X86_CPUID_BIT(0x7, 1, 5)
#define X86_FEATURE_SMEP         X86_CPUID_BIT(0x7, 1, 7)
#define X86_FEATURE_RDSEED       X86_CPUID_BIT(0x7, 1, 18)
#define X86_FEATURE_SMAP         X86_CPUID_BIT(0x7, 1, 20)
#define X86_FEATURE_PT           X86_CPUID_BIT(0x7, 1, 25)
#define X86_FEATURE_PKU          X86_CPUID_BIT(0x7, 2, 3)
#define X86_FEATURE_AMD_TOPO     X86_CPUID_BIT(0x80000001, 2, 22)
#define X86_FEATURE_SYSCALL      X86_CPUID_BIT(0x80000001, 3, 11)
#define X86_FEATURE_NX           X86_CPUID_BIT(0x80000001, 3, 20)
#define X86_FEATURE_HUGE_PAGE    X86_CPUID_BIT(0x80000001, 3, 26)
#define X86_FEATURE_RDTSCP       X86_CPUID_BIT(0x80000001, 3, 27)
#define X86_FEATURE_INVAR_TSC    X86_CPUID_BIT(0x80000007, 3, 8)

/* legacy accessors */
static inline uint8_t x86_linear_address_width(void)
{
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return 0;

    /*
     Extracting bit 15:8 from eax register
     Bits 15-08: #Linear Address Bits
    */
    return (leaf->a >> 8) & 0xff;
}

static inline uint8_t x86_physical_address_width(void)
{
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return 0;

    /*
     Extracting bit 7:0 from eax register
     Bits 07-00: #Physical Address Bits
    */
    return leaf->a & 0xff;
}

/* cpu vendors */
enum x86_vendor_list {
    X86_VENDOR_UNKNOWN,
    X86_VENDOR_INTEL,
    X86_VENDOR_AMD
};

extern enum x86_vendor_list x86_vendor;

/* topology */

#define X86_TOPOLOGY_INVALID 0
#define X86_TOPOLOGY_SMT 1
#define X86_TOPOLOGY_CORE 2

struct x86_topology_level {
    /* The number of bits to right shift to identify the next-higher topological
     * level */
    uint8_t right_shift;
    /* The type of relationship this level describes (hyperthread/core/etc) */
    uint8_t type;
};

/**
 * @brief Fetch the topology information for the given level.
 *
 * This interface is uncached.
 *
 * @param level The level to retrieve info for.  Should initially be 0 and
 * incremented with each call.
 * @param info The structure to populate with the discovered information
 *
 * @return true if the requested level existed (and there may be higher levels).
 * @return false if the requested level does not exist (and no higher ones do).
 */
bool x86_topology_enumerate(uint8_t level, struct x86_topology_level *info);

struct x86_model_info {
    uint8_t processor_type;
    uint8_t family;
    uint8_t model;
    uint8_t stepping;

    uint32_t display_family;
    uint32_t display_model;
};

const struct x86_model_info * x86_get_model(void);

enum x86_microarch_list {
    X86_MICROARCH_UNKNOWN,
    X86_MICROARCH_INTEL_SANDY_BRIDGE,
    X86_MICROARCH_INTEL_IVY_BRIDGE,
    X86_MICROARCH_INTEL_BROADWELL,
    X86_MICROARCH_INTEL_HASWELL,
    X86_MICROARCH_INTEL_SKYLAKE,
    X86_MICROARCH_INTEL_KABYLAKE,
    X86_MICROARCH_AMD_BULLDOZER,
    X86_MICROARCH_AMD_JAGUAR,
    X86_MICROARCH_AMD_ZEN,
};
extern enum x86_microarch_list x86_microarch;

__END_CDECLS
