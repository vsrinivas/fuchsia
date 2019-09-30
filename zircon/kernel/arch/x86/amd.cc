// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/cpuid.h>
#include <arch/x86/feature.h>

uint32_t x86_amd_get_patch_level(void) {
  uint32_t patch_level = 0;
  if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    patch_level = static_cast<uint32_t>(read_msr(X86_MSR_IA32_BIOS_SIGN_ID));
  }
  return patch_level;
}

bool x86_amd_cpu_has_ssb(const cpu_id::CpuId* cpuid, MsrAccess* msr) {
  // Future AMD processors may set CPUID Fn8000_0008 EBX[26] to indicate memory disambiguation may
  // not be used to leak data from memory.
  //
  // See https://developer.amd.com/wp-content/resources/124441_AMD64_SpeculativeStoreBypassDisable_Whitepaper_final.pdf
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::AMD_SSB_NO)) {
    return false;
  }

  auto* const microarch_config = get_microarch_config(cpuid);
  return microarch_config->has_ssb;
}

void x86_amd_init_percpu(void) {}
