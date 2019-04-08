// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <bits.h>

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
