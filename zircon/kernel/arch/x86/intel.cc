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
#include <iovec.h>
#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>

static SpinLock g_microcode_lock;

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

static uint32_t microcode_checksum(uint32_t* patch, size_t dwords) {
    uint32_t sum = 0;
    for (size_t i = 0; i < dwords; i++) {
        sum += patch[i];
    }
    return sum;
}

bool x86_intel_check_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, struct iovec patch) {
    // See Intel SDM Volume 3 9.11 "Microcode Update Facilities"
    const uint32_t processor_signature = cpuid->ReadProcessorId().signature();
    const uint64_t platform_id = msr->read_msr(X86_MSR_IA32_PLATFORM_ID);

    auto* const hdr = reinterpret_cast<const x86_intel_microcode_update_header*>(patch.iov_base);
    // All Intel microcode patches released so far have a header version of 0x1.
    if (hdr->header_version != 0x1) {
        return false;
    }
    // Check if this patch is for this processor.
    if (hdr->processor_signature != processor_signature) {
        return false;
    }
    const uint64_t platform_id_bits = BITS_SHIFT(platform_id, 52, 50);
    const bool flags_match = hdr->processor_flags & (1 << platform_id_bits);
    if (!flags_match) {
        return false;
    }

    const auto dwords = patch.iov_len / 4;
    const uint32_t checksum = microcode_checksum(reinterpret_cast<uint32_t*>(patch.iov_base), dwords);
    if (checksum != 0) {
       return false;
    }

    return true;
}

// Attempt to load a compatible microcode patch. Invoked on every logical processor.
void x86_intel_load_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, struct iovec patch) {
    AutoSpinLock lock(&g_microcode_lock);

    auto* const hdr = reinterpret_cast<const x86_intel_microcode_update_header*>(patch.iov_base);
    const uint32_t current_patch_level = x86_intel_get_patch_level();
    // Skip patch if we already have a newer version loaded. This is not required
    // but does save many cycles, especially on hyperthreaded CPUs.
    if (hdr->update_revision <= current_patch_level) {
        return;
    }

    const uintptr_t data = reinterpret_cast<uintptr_t>(static_cast<char*>(patch.iov_base) +
        sizeof(*hdr));
    // Write back & invalidate caches before loading microcode; this is not necessary
    // per the SDM, but Intel posts to LKML indicate it may be required.
    asm volatile("wbinvd" ::: "memory");
    msr->write_msr(X86_MSR_IA32_BIOS_UPDT_TRIG, data);
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

bool x86_intel_cpu_has_meltdown(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
    // IA32_ARCH_CAPABILITIES MSR enumerates fixes for Meltdown and other speculation-related side
    // channels, where available.
    if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
      uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
      if (arch_capabilities & X86_ARCH_CAPABILITIES_RDCL_NO) {
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
        if (arch_capabilities & X86_ARCH_CAPABILITIES_RDCL_NO) {
            return false;
        }
    }

    return true;
}

// Returns true iff the CPU is susceptible to any variant of MDS.
bool x86_intel_cpu_has_mds(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
    // MDS is a family of speculative execution information disclosure vulnerabilities affecting
    // many CPUs.
    // https://www.intel.com/content/www/us/en/architecture-and-technology/mds.html

    auto info = cpuid->ReadProcessorId();
    if (info.family() == 6) {
        switch (info.model()) {
        // Bonnell, Saltwell, Goldmont, GoldmontPlus, Tremont are not affected by any variant.
        case 0x1c:  // Bonnell
        case 0x26:  // Bonnell
        case 0x27:  // Saltwell
        case 0x35:  // Saltwell
        case 0x36:  // Saltwell
        case 0x5c:  // Goldmont (Apollo Lake)
        case 0x7a:  // Goldmont Plus (Gemini Lake)
        case 0x86:  // Tremont
            return false;
        // Cascade Lake steppings 6, 7 and Whiskey Lake steppings C and D
        // are unaffected by any variant but enumerate MDS_NO.
        }
    }

    if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
        uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
        if (arch_capabilities & X86_ARCH_CAPABILITIES_MDS_NO) {
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
