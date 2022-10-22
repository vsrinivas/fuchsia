// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "amd.h"

#include <arch/x86.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>
#include <arch/x86/platform_access.h>
#include <kernel/mp.h>

uint32_t x86_amd_get_patch_level(void) {
  uint32_t patch_level = 0;
  if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    patch_level = static_cast<uint32_t>(read_msr(X86_MSR_IA32_BIOS_SIGN_ID));
  }
  return patch_level;
}

void x86_amd_zen2_retbleed_mitigation(const x86_model_info& model) {
  constexpr uint32_t kDeCfg2 = 0xC00110E3;
  constexpr uint64_t kSuppressBPOnNonBr = (1 << 1);
  MsrAccess msr;

  // Zen 2 processors have a configuration bit that mitigates the BTC-NOBR case
  // of the Branch Type Confusion / retbleed vulnerability.
  if (x86_feature_test(X86_FEATURE_HYPERVISOR))
    return;
  if ((model.display_family == 0x17) &&
      ((model.display_model >= 0x30 && model.display_model <= 0x4F) ||
      (model.display_model >= 0x60 && model.display_model <= 0x7F))) {
    uint64_t de_cfg2 = msr.read_msr(kDeCfg2);
    if (!(de_cfg2 & kSuppressBPOnNonBr)) {
      msr.write_msr(kDeCfg2, de_cfg2 | kSuppressBPOnNonBr);
    }
  }
}

void x86_amd_set_lfence_serializing(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  // "Software Techniques for Managing Speculation on AMD Processors"
  // Mitigation G-2: Set MSR so that LFENCE is a dispatch-serializing instruction.
  //
  // To mitigate certain speculative execution infoleaks (Spectre) efficiently, configure the
  // CPU to treat LFENCE as a dispatch serializing instruction. This allows code to use LFENCE
  // in contexts to restrict speculative execution.
  if (cpuid->ReadProcessorId().family() >= 0x10) {
    uint64_t de_cfg = msr->read_msr(X86_MSR_AMD_F10_DE_CFG);
    if (!(de_cfg & X86_MSR_AMD_F10_DE_CFG_LFENCE_SERIALIZE)) {
      msr->write_msr(X86_MSR_AMD_F10_DE_CFG, de_cfg | X86_MSR_AMD_F10_DE_CFG_LFENCE_SERIALIZE);
    }
  }
}

// All Bulldozer and Zen 1 / Zen 2 CPUs are affected by Retbleed.
bool x86_amd_has_retbleed() {
  cpu_id::CpuId cpuid;

  // TODO: Check BTC_NO bit.
  auto cpu = cpuid.ReadProcessorId();
  if (cpu.family() == 0x15 || cpu.family() == 0x17) {
    return true;
  }
  return false;
}

void x86_amd_init_percpu(void) {
  cpu_id::CpuId cpuid;
  MsrAccess msr;
  x86_amd_set_lfence_serializing(&cpuid, &msr);
}
