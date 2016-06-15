// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/feature.h>

#include <trace.h>
#include <stdint.h>
#include <assert.h>

#include <arch/ops.h>

#define LOCAL_TRACE 0

#define MAX_SUPPORTED_CPUID     (0x17)
#define MAX_SUPPORTED_CPUID_EXT (0x80000008)

static struct cpuid_leaf _cpuid[MAX_SUPPORTED_CPUID + 1];
static struct cpuid_leaf _cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
static uint32_t max_cpuid = 0;
static uint32_t max_ext_cpuid = 0;

static int initialized = 0;

void x86_feature_init(void)
{
    if (atomic_swap(&initialized, 1)) {
        return;
    }
    /* test for cpuid count */
    cpuid(0, &_cpuid[0].a, &_cpuid[0].b, &_cpuid[0].c, &_cpuid[0].d);

    max_cpuid = _cpuid[0].a;
    if (max_cpuid > MAX_SUPPORTED_CPUID)
        max_cpuid = MAX_SUPPORTED_CPUID;
    LTRACEF("max cpuid 0x%x\n", max_cpuid);

    /* read in the base cpuids */
    for (uint32_t i = 1; i <= max_cpuid; i++) {
        cpuid_c(i, 0, &_cpuid[i].a, &_cpuid[i].b, &_cpuid[i].c, &_cpuid[i].d);
    }

    /* test for extended cpuid count */
    cpuid(X86_CPUID_EXT_BASE, &_cpuid_ext[0].a, &_cpuid_ext[0].b, &_cpuid_ext[0].c, &_cpuid_ext[0].d);

    max_ext_cpuid = _cpuid_ext[0].a;
    LTRACEF("max extended cpuid 0x%x\n", max_ext_cpuid);
    if (max_ext_cpuid > MAX_SUPPORTED_CPUID_EXT)
        max_ext_cpuid = MAX_SUPPORTED_CPUID_EXT;

    /* read in the extended cpuids */
    for (uint32_t i = X86_CPUID_EXT_BASE + 1; i - 1 < max_ext_cpuid; i++) {
        uint32_t index = i - X86_CPUID_EXT_BASE;
        cpuid_c(i, 0, &_cpuid_ext[index].a, &_cpuid_ext[index].b, &_cpuid_ext[index].c, &_cpuid_ext[index].d);
    }

#if LK_DEBUGLEVEL > 1
    x86_feature_debug();
#endif
}

const struct cpuid_leaf *x86_get_cpuid_leaf(enum x86_cpuid_leaf_num leaf)
{
    if (leaf < X86_CPUID_EXT_BASE) {
        if (leaf > max_cpuid)
            return NULL;

        return &_cpuid[leaf];
    } else {
        if (leaf > max_ext_cpuid)
            return NULL;

        leaf -= X86_CPUID_EXT_BASE;
        return &_cpuid_ext[leaf];
    }
}

bool x86_get_cpuid_subleaf(
        enum x86_cpuid_leaf_num num, uint32_t subleaf, struct cpuid_leaf *leaf)
{
    if (num < X86_CPUID_EXT_BASE) {
        if (num > max_cpuid)
            return false;
    } else if (num > max_ext_cpuid) {
        return false;
    }

    cpuid_c((uint32_t)num, subleaf, &leaf->a, &leaf->b, &leaf->c, &leaf->d);
    return true;
}

bool x86_feature_test(struct x86_cpuid_bit bit)
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

bool x86_topology_enumerate(uint8_t level, struct x86_topology_level *info)
{
    DEBUG_ASSERT(info);

    uint32_t eax, ebx, ecx, edx;
    cpuid_c(X86_CPUID_TOPOLOGY, level, &eax, &ebx, &ecx, &edx);

    uint8_t type = (ecx >> 8) & 0xff;
    if (type == X86_TOPOLOGY_INVALID) {
        return false;
    }

    info->num_bits = eax & 0x1f;
    info->type = type;
    return true;
}

void x86_feature_debug(void)
{
    printf("Features:");
    if (x86_feature_test(X86_FEATURE_FPU))
        printf(" fpu");
    if (x86_feature_test(X86_FEATURE_SSE))
        printf(" sse");
    if (x86_feature_test(X86_FEATURE_SSE2))
        printf(" sse2");
    if (x86_feature_test(X86_FEATURE_SSE3))
        printf(" sse3");
    if (x86_feature_test(X86_FEATURE_SSSE3))
        printf(" ssse3");
    if (x86_feature_test(X86_FEATURE_SSE4_1))
        printf(" sse4.1");
    if (x86_feature_test(X86_FEATURE_SSE4_2))
        printf(" sse4.2");
    if (x86_feature_test(X86_FEATURE_MMX))
        printf(" mmx");
    if (x86_feature_test(X86_FEATURE_AVX))
        printf(" avx");
    if (x86_feature_test(X86_FEATURE_AVX2))
        printf(" avx2");
    if (x86_feature_test(X86_FEATURE_FXSR))
        printf(" fxsr");
    if (x86_feature_test(X86_FEATURE_XSAVE))
        printf(" xsave");
    if (x86_feature_test(X86_FEATURE_AESNI))
        printf(" aesni");
    if (x86_feature_test(X86_FEATURE_TSC_ADJUST))
        printf(" tsc_adj");
    if (x86_feature_test(X86_FEATURE_SMEP))
        printf(" smep");
    if (x86_feature_test(X86_FEATURE_SMAP))
        printf(" smap");
    if (x86_feature_test(X86_FEATURE_RDRAND))
        printf(" rdrand");
    if (x86_feature_test(X86_FEATURE_RDSEED))
        printf(" rdseed");
    if (x86_feature_test(X86_FEATURE_PKU))
        printf(" pku");
    if (x86_feature_test(X86_FEATURE_SYSCALL))
        printf(" syscall");
    if (x86_feature_test(X86_FEATURE_NX))
        printf(" nx");
    if (x86_feature_test(X86_FEATURE_HUGE_PAGE))
        printf(" huge");
    if (x86_feature_test(X86_FEATURE_RDTSCP))
        printf(" rdtscp");
    if (x86_feature_test(X86_FEATURE_INVAR_TSC))
        printf(" invar_tsc");
    printf("\n");
}
