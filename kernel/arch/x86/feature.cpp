// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/feature.h>

#include <assert.h>
#include <bits.h>
#include <stdint.h>
#include <string.h>
#include <trace.h>

#include <arch/ops.h>

#include <fbl/algorithm.h>

#define LOCAL_TRACE 0

struct cpuid_leaf _cpuid[MAX_SUPPORTED_CPUID + 1];
struct cpuid_leaf _cpuid_ext[MAX_SUPPORTED_CPUID_EXT - X86_CPUID_EXT_BASE + 1];
uint32_t max_cpuid = 0;
uint32_t max_ext_cpuid = 0;

enum x86_vendor_list x86_vendor;
enum x86_microarch_list x86_microarch;

static struct x86_model_info model_info;

bool g_x86_feature_smap;

static int initialized = 0;

static enum x86_microarch_list get_microarch(struct x86_model_info* info);

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

    /* figure out the vendor */
    union {
        uint32_t vendor_id[3];
        char vendor_string[13];
    } vu;
    vu.vendor_id[0] = _cpuid[0].b;
    vu.vendor_id[1] = _cpuid[0].d;
    vu.vendor_id[2] = _cpuid[0].c;
    vu.vendor_string[12] = '\0';
    if (!strcmp(vu.vendor_string, "GenuineIntel")) {
        x86_vendor = X86_VENDOR_INTEL;
    } else if (!strcmp(vu.vendor_string, "AuthenticAMD")) {
        x86_vendor = X86_VENDOR_AMD;
    } else {
        x86_vendor = X86_VENDOR_UNKNOWN;
    }

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

    /* populate the model info */
    const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_MODEL_FEATURES);
    if (leaf) {
        model_info.processor_type = (uint8_t)BITS_SHIFT(leaf->a, 13, 12);
        model_info.family = (uint8_t)BITS_SHIFT(leaf->a, 11, 8);
        model_info.model = (uint8_t)BITS_SHIFT(leaf->a, 7, 4);
        model_info.stepping = (uint8_t)BITS_SHIFT(leaf->a, 3, 0);
        model_info.display_family = model_info.family;
        model_info.display_model = model_info.model;

        if (model_info.family == 0xf) {
            model_info.display_family += BITS_SHIFT(leaf->a, 27, 20);
        }

        if (model_info.family == 0xf || model_info.family == 0x6) {
            model_info.display_model += BITS_SHIFT(leaf->a, 19, 16) << 4;
        }

        x86_microarch = get_microarch(&model_info);
    }

    g_x86_feature_smap = x86_feature_test(X86_FEATURE_SMAP);
}

static enum x86_microarch_list get_microarch(struct x86_model_info* info) {
    if (x86_vendor == X86_VENDOR_INTEL && info->family == 0x6) {
        switch (info->display_model) {
            case 0x2a: /* Sandy Bridge */
            case 0x2d: /* Sandy Bridge EP */
                return X86_MICROARCH_INTEL_SANDY_BRIDGE;
            case 0x3a: /* Ivy Bridge */
            case 0x3e: /* Ivy Bridge EP */
                return X86_MICROARCH_INTEL_IVY_BRIDGE;
            case 0x3c: /* Haswell DT */
            case 0x3f: /* Haswell MB */
            case 0x45: /* Haswell ULT */
            case 0x46: /* Haswell ULX */
                return X86_MICROARCH_INTEL_HASWELL;
            case 0x3d: /* Broadwell */
            case 0x47: /* Broadwell H */
            case 0x56: /* Broadwell EP */
            case 0x4f: /* Broadwell EX */
                return X86_MICROARCH_INTEL_BROADWELL;
            case 0x4e: /* Skylake Y/U */
            case 0x5e: /* Skylake H/S */
            case 0x55: /* Skylake E */
                return X86_MICROARCH_INTEL_SKYLAKE;
            case 0x8e: /* Kabylake Y/U */
            case 0x9e: /* Kabylake H/S */
                return X86_MICROARCH_INTEL_KABYLAKE;
        }
    } else if (x86_vendor == X86_VENDOR_AMD && info->family == 0xf) {
        switch (info->display_family) { // zen
            case 0x15: /* Bulldozer */
                return X86_MICROARCH_AMD_BULLDOZER;
            case 0x16: /* Jaguar */
                return X86_MICROARCH_AMD_JAGUAR;
            case 0x17: /* Zen */
                return X86_MICROARCH_AMD_ZEN;
        }
    }
    return X86_MICROARCH_UNKNOWN;
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

bool x86_topology_enumerate(uint8_t level, struct x86_topology_level *info)
{
    DEBUG_ASSERT(info);

    uint32_t eax, ebx, ecx, edx;
    cpuid_c(X86_CPUID_TOPOLOGY, level, &eax, &ebx, &ecx, &edx);

    uint8_t type = (ecx >> 8) & 0xff;
    if (type == X86_TOPOLOGY_INVALID) {
        return false;
    }

    info->right_shift = eax & 0x1f;
    info->type = type;
    return true;
}

const struct x86_model_info * x86_get_model(void)
{
    return &model_info;
}

void x86_feature_debug(void)
{
    static struct {
        struct x86_cpuid_bit bit;
        const char *name;
    } features[] = {
        { X86_FEATURE_FPU, "fpu" },
        { X86_FEATURE_SSE, "sse" },
        { X86_FEATURE_SSE2, "sse2" },
        { X86_FEATURE_SSE3, "sse3" },
        { X86_FEATURE_SSSE3, "ssse3" },
        { X86_FEATURE_SSE4_1, "sse4.1" },
        { X86_FEATURE_SSE4_2, "sse4.2" },
        { X86_FEATURE_MMX, "mmx" },
        { X86_FEATURE_AVX, "avx" },
        { X86_FEATURE_AVX2, "avx2" },
        { X86_FEATURE_FXSR, "fxsr" },
        { X86_FEATURE_XSAVE, "xsave" },
        { X86_FEATURE_AESNI, "aesni" },
        { X86_FEATURE_FSGSBASE, "fsgsbase" },
        { X86_FEATURE_TSC_ADJUST, "tsc_adj" },
        { X86_FEATURE_SMEP, "smep" },
        { X86_FEATURE_SMAP, "smap" },
        { X86_FEATURE_RDRAND, "rdrand" },
        { X86_FEATURE_RDSEED, "rdseed" },
        { X86_FEATURE_PKU, "pku" },
        { X86_FEATURE_SYSCALL, "syscall" },
        { X86_FEATURE_NX, "nx" },
        { X86_FEATURE_HUGE_PAGE, "huge" },
        { X86_FEATURE_RDTSCP, "rdtscp" },
        { X86_FEATURE_INVAR_TSC, "invar_tsc" },
        { X86_FEATURE_TSC_DEADLINE, "tsc_deadline" },
        { X86_FEATURE_VMX, "vmx" },
        { X86_FEATURE_HYPERVISOR, "hypervisor" },
        { X86_FEATURE_PT, "pt" },
        { X86_FEATURE_HWP, "hwp" },
    };

    const char *vendor_string = NULL;
    switch (x86_vendor) {
        case X86_VENDOR_UNKNOWN: vendor_string = "unknown"; break;
        case X86_VENDOR_INTEL: vendor_string = "Intel"; break;
        case X86_VENDOR_AMD: vendor_string = "AMD"; break;
    }
    printf("Vendor: %s\n", vendor_string);

    const char *microarch_string = NULL;
    switch (x86_microarch) {
        case X86_MICROARCH_UNKNOWN: microarch_string = "unknown"; break;
        case X86_MICROARCH_INTEL_SANDY_BRIDGE: microarch_string = "Sandy Bridge"; break;
        case X86_MICROARCH_INTEL_IVY_BRIDGE: microarch_string = "Ivy Bridge"; break;
        case X86_MICROARCH_INTEL_BROADWELL: microarch_string = "Broadwell"; break;
        case X86_MICROARCH_INTEL_HASWELL: microarch_string = "Haswell"; break;
        case X86_MICROARCH_INTEL_SKYLAKE: microarch_string = "Skylake"; break;
        case X86_MICROARCH_INTEL_KABYLAKE: microarch_string = "Kaby Lake"; break;
        case X86_MICROARCH_AMD_BULLDOZER: microarch_string = "Bulldozer"; break;
        case X86_MICROARCH_AMD_JAGUAR: microarch_string = "Jaguar"; break;
        case X86_MICROARCH_AMD_ZEN: microarch_string = "Zen"; break;
    }
    printf("Microarch: %s\n", microarch_string);

    printf("Features: ");
    uint col = 0;
    for (uint i = 0; i < fbl::count_of(features); ++i) {
        if (x86_feature_test(features[i].bit))
            col += printf("%s ", features[i].name);
        if (col >= 80) {
            printf("\n");
            col = 0;
        }
    }
    if (col > 0)
        printf("\n");

}
