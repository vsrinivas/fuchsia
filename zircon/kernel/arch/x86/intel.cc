// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/cmdline.h>
#include <lib/code_patching.h>

#include <arch/x86.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>
#include <arch/x86/hwp.h>
#include <arch/x86/platform_access.h>
#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>
#include <kernel/mp.h>

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

bool x86_intel_idle_state_may_empty_rsb(X86IdleState* state) {
  const x86_microarch_config_t* const microarch = x86_get_microarch_config();
  switch (microarch->x86_microarch) {
  // C-states deeper than C6 may empty the return stack buffer on certain CPUs.
  // Sequences of code that are sensitive to empty RSBs may wish to refill it when it is emptied;
  // return true if a selected idle state may drain this structure.
  case X86_MICROARCH_INTEL_SKYLAKE:
    return state->MwaitHint() >= 0x20;
  default:
    return false;
  }
}

bool x86_intel_check_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, zx_iovec_t patch) {
  // See Intel SDM Volume 3 9.11 "Microcode Update Facilities"
  const uint32_t processor_signature = cpuid->ReadProcessorId().signature();
  const uint64_t platform_id = msr->read_msr(X86_MSR_IA32_PLATFORM_ID);

  auto* const hdr = reinterpret_cast<const x86_intel_microcode_update_header*>(patch.buffer);
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

  const auto dwords = patch.capacity / 4;
  const uint32_t checksum = microcode_checksum(reinterpret_cast<uint32_t*>(patch.buffer), dwords);
  if (checksum != 0) {
    return false;
  }

  return true;
}

// Attempt to load a compatible microcode patch. Invoked on every logical processor.
void x86_intel_load_microcode_patch(cpu_id::CpuId* cpuid, MsrAccess* msr, zx_iovec_t patch) {
  AutoSpinLock lock(&g_microcode_lock);

  auto* const hdr = reinterpret_cast<const x86_intel_microcode_update_header*>(patch.buffer);
  const uint32_t current_patch_level = x86_intel_get_patch_level();
  // Skip patch if we already have a newer version loaded. This is not required
  // but does save many cycles, especially on hyperthreaded CPUs.
  if (hdr->update_revision <= current_patch_level) {
    return;
  }

  const uintptr_t data =
      reinterpret_cast<uintptr_t>(static_cast<char*>(patch.buffer) + sizeof(*hdr));
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
        : "ebx", "ecx", "memory");
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

  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_meltdown;
}

bool x86_intel_cpu_has_l1tf(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  // IA32_ARCH_CAPABILITIES MSR enumerates fixes for L1TF, if available.
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    if (arch_capabilities & X86_ARCH_CAPABILITIES_RDCL_NO) {
      return false;
    }
  }

  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_l1tf;
}

// Returns true iff the CPU is susceptible to any variant of MDS or TAA.
bool x86_intel_cpu_has_mds_taa(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  // MDS is a family of speculative execution information disclosure vulnerabilities affecting
  // many CPUs.
  // https://www.intel.com/content/www/us/en/architecture-and-technology/mds.html
  //
  // TAA is a related vulnerability which uses TSX extensions
  // https://www.intel.com/content/www/us/en/security-center/advisory/intel-sa-00270.html

  // A processor has MDS if:
  // 1. arch_capabilities is present and MDS_NO = 0 OR
  // 2. arch_capabilities is not present and static tables indicate the processor family has MDS.
  // A processor has TAA if:
  // 0. TSX is available AND
  // 1. arch_capabilities is present and TAA_NO = 0 OR
  // 2. arch_capabilities is not present
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    if ((arch_capabilities & X86_ARCH_CAPABILITIES_MDS_NO) &&
        (arch_capabilities & X86_ARCH_CAPABILITIES_TAA_NO)) {
      return false;
    }
  }
  bool has_tsx = cpuid->ReadFeatures().HasFeature(cpu_id::Features::HLE) ||
                 cpuid->ReadFeatures().HasFeature(cpu_id::Features::RTM);

  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_mds || has_tsx;
}

bool x86_intel_cpu_has_swapgs_bug(const cpu_id::CpuId* cpuid) {
  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_swapgs_bug;
}

bool x86_intel_cpu_has_rsb_fallback(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    if (arch_capabilities & X86_ARCH_CAPABILITIES_RSBA) {
      return true;
    }
  }

  auto* const microarch_config = get_microarch_config(cpuid);
  return (microarch_config->x86_microarch == X86_MICROARCH_INTEL_SKYLAKE) ||
         (microarch_config->x86_microarch == X86_MICROARCH_INTEL_CANNONLAKE) ||
         (microarch_config->x86_microarch == X86_MICROARCH_UNKNOWN);
}

bool x86_intel_cpu_has_ssb(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    if (arch_capabilities & X86_ARCH_CAPABILITIES_SSB_NO) {
      return false;
    }
  }

  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_ssb;
}

bool x86_intel_cpu_has_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  return cpuid->ReadFeatures().HasFeature(cpu_id::Features::SSBD);
}

void x86_intel_cpu_set_ssbd(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::SSBD)) {
    uint64_t spec_ctrl = msr->read_msr(X86_MSR_IA32_SPEC_CTRL);
    spec_ctrl |= X86_SPEC_CTRL_SSBD;
    msr->write_msr(/*msr_index=*/X86_MSR_IA32_SPEC_CTRL, /*value=*/spec_ctrl);
  }
}

bool x86_intel_cpu_has_enhanced_ibrs(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    return arch_capabilities & X86_ARCH_CAPABILITIES_IBRS_ALL;
  }
  return false;
}

void x86_intel_cpu_set_turbo(const cpu_id::CpuId* cpu, MsrAccess* msr, Turbostate state) {
  if (cpu->ReadFeatures().HasFeature(cpu_id::Features::HYPERVISOR)) {
    return;
  }
  if (!cpu->ReadFeatures().HasFeature(cpu_id::Features::TURBO)) {
    return;
  }

  uint64_t value = msr->read_msr(/*msr_index=*/X86_MSR_IA32_MISC_ENABLE);
  uint64_t new_value = value;
  switch (state) {
    case Turbostate::ENABLED:
      new_value &= ~(X86_MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
      break;
    case Turbostate::DISABLED:
      new_value |= (X86_MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
      break;
  }
  if (new_value != value) {
    msr->write_msr(/*msr_index=*/X86_MSR_IA32_MISC_ENABLE, /*value=*/new_value);
  }
}

// Disable TSX if possible.
void x86_intel_cpu_try_disable_tsx(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::ARCH_CAPABILITIES)) {
    uint64_t arch_capabilities = msr->read_msr(X86_MSR_IA32_ARCH_CAPABILITIES);
    if (!(arch_capabilities & X86_ARCH_CAPABILITIES_TSX_CTRL)) {
      return;
    }

    msr->write_msr(/*index=*/X86_MSR_IA32_TSX_CTRL,
                   X86_TSX_CTRL_RTM_DISABLE | X86_TSX_CTRL_CPUID_DISABLE);
  }
}

void x86_intel_init_percpu(void) {
  cpu_id::CpuId cpuid;

  // Some intel cpus support auto-entering C1E state when all cores are at C1. In
  // C1E state the voltage is reduced on all cores as well as clock gated. There is
  // a latency associated with ramping the voltage on wake. Disable this feature here
  // to save time on the irq path from idle. (5-10us on skylake nuc from kernel irq
  // handler to user space handler).
  if (!x86_feature_test(X86_FEATURE_HYPERVISOR) && x86_get_microarch_config()->disable_c1e) {
    uint64_t power_ctl_msr = read_msr(X86_MSR_POWER_CTL);
    write_msr(0x1fc, power_ctl_msr & ~0x2);
  }
}

extern "C" void x86_mds_flush_select(const CodePatchInfo* patch) {
  const size_t kSize = 5;
  DEBUG_ASSERT(patch->dest_size == kSize);

  extern bool g_md_clear_on_user_return;
  if (!g_md_clear_on_user_return) {
    memset(patch->dest_addr, 0x90, kSize);
  } else {
    // Keep the call to mds_buf_overwrite in place.
  }
}
