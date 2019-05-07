// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <bits.h>

struct x86_intel_microcode_update_header {
    uint32_t header_version;
    uint32_t update_revision;
    uint32_t date;
    uint32_t processor_signature;
    uint32_t checksum;
    uint32_t loader_revision;
    uint32_t processor_flags;
    uint32_t data_size;
    uint32_t total_size;
    uint32_t reserved[3];
};

static uint32_t x86_intel_processor_signature(void) {
    uint32_t eax, ebx, ecx, edx;
    __cpuid(X86_CPUID_MODEL_FEATURES, eax, ebx, ecx, edx);
    return eax;
}

// Attempt to load a compatible microcode patch, if applicable.
// Invoked on every logical processor before CPUID leaves are cached.
void x86_intel_load_microcode_patch(void) {
    // See Intel SDM Volume 3 9.11 "Microcode Update Facilities"
}

uint32_t x86_intel_get_patch_level(void) {
    uint32_t patch_level = 0;
    if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
        uint32_t dummy;
        // Invoking CPUID for leaf 1h fills in the microcode patch level into the high half of
        // X86_MSR_IA32_BIOS_SIGN_ID MSR. Operations between CPUID and RDMSR may clear the MSR;
        // write this sequence in assembly to ensure that there are none.
        asm volatile(
            "xorl %%eax, %%eax\n"
            "xorl %%edx, %%edx\n"
            "movl $0x8b, %%ecx\n"
            "wrmsr\n"  // Clear X86_MSR_IA32_BIOS_SIGN_ID before reading the patch level, per SDM.
            "movq $0x1, %%rax\n"
            "cpuid\n"
            "movl $0x8b, %%ecx\n"
            "rdmsr\n"
            : "=a"(dummy), "=d"(patch_level)
            :
            : "ebx", "ecx", "memory"
        );
    }
    return patch_level;
}

bool x86_intel_cpu_has_meltdown(void) {
    // IA32_ARCH_CAPABILITIES MSR enumerates fixes for Meltdown and other speculation-related side
    // channels, where available.
    const auto* leaf = x86_get_cpuid_leaf(X86_CPUID_EXTENDED_FEATURE_FLAGS);
    if (leaf && BIT(leaf->d, 29)) {
        uint64_t arch_capabilities = read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
        if (BIT(arch_capabilities, X86_ARCH_CAPABILITIES_RDCL_NO)) {
            return false;
        }
    }

    return true;
}

bool x86_intel_cpu_has_l1tf(void) {
    // Silvermont/Airmont/Goldmont are not affected by L1TF.
    auto* const info = x86_get_model();
    if (info->family == 6 && info->model == 0x4C) {
        return false;
    }

    const auto* leaf = x86_get_cpuid_leaf(X86_CPUID_EXTENDED_FEATURE_FLAGS);
    if (leaf && BIT(leaf->d, 29)) {
        uint64_t arch_capabilities = read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
        if (BIT(arch_capabilities, X86_ARCH_CAPABILITIES_RDCL_NO)) {
            return false;
        }
    }

    return true;
}

void x86_intel_init_percpu(void) {
    // Some intel cpus support auto-entering C1E state when all cores are at C1. In
    // C1E state the voltage is reduced on all cores as well as clock gated. There is
    // a latency associated with ramping the voltage on wake. Disable this feature here
    // to save time on the irq path from idle. (5-10us on skylake nuc from kernel irq
    // handler to user space handler).
    if (!x86_feature_test(X86_FEATURE_HYPERVISOR) &&
        x86_get_microarch_config()->disable_c1e) {
        uint64_t power_ctl_msr = read_msr(X86_MSR_POWER_CTL);
        write_msr(0x1fc, power_ctl_msr & ~0x2);
    }
}
