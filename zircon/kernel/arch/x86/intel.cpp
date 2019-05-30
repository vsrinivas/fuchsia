// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <bits.h>

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

bool x86_intel_cpu_has_meltdown(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
    // IA32_ARCH_CAPABILITIES MSR enumerates fixes for Meltdown and other speculation-related side
    // channels, where available.
    if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
      uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
      if (BIT(arch_capabilities, X86_ARCH_CAPABILITIES_RDCL_NO)) {
        return false;
      }
    }

    return true;
}

bool x86_intel_cpu_has_l1tf(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
    // Silvermont/Airmont/Goldmont are not affected by L1TF.
    auto info = cpuid->ReadProcessorId();
    if (info.family() == 6 && info.model() == 0x4C) {
        return false;
    }

    if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
        uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
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
