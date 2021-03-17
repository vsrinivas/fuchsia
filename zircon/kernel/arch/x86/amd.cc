// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

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

bool x86_amd_cpu_has_ibrs_always_on(const cpu_id::CpuId* cpuid) {
  if (cpuid->ReadFeatures().HasFeature(cpu_id::Features::AMD_IBRS_ALWAYS_ON) &&
      cpuid->ReadFeatures().HasFeature(cpu_id::Features::AMD_PREFER_IBRS)) {
    return true;
  }
  return false;
}

void x86_amd_init_percpu_17h_zen1_quirks(cpu_id::CpuId* cpuid, MsrAccess* msr) {
  // See: Revision Guide for AMD Family 17h Models 00h-0Fh Processors, #55449
  auto processor_id = cpuid->ReadProcessorId();

  // 1021: Load Operation May Receive Stale Data From Older Store Operation
  uint64_t value = msr->read_msr(0xC001'1029);
  value |= (1ull << 13);
  msr->write_msr(0xC001'1029, value);

  // 1033: A Lock Operation May Cause the System to Hang
  if (processor_id.model() == 0x1 && processor_id.stepping() == 0x1) {
    value = msr->read_msr(X86_MSR_AMD_LS_CFG);
    value |= (1ull << 4);
    msr->write_msr(X86_MSR_AMD_LS_CFG, value);
  }

  // 1049: FCMOV Instruction May Not Execute Correctly
  value = msr->read_msr(0xC001'1028);
  value |= (1ull << 4);
  msr->write_msr(0xC001'1028, value);

  // 1090
  if (processor_id.model() == 0x1 && processor_id.stepping() == 0x1) {
    value = msr->read_msr(0xC001'1023);
    value |= (1ull << 8);
    msr->write_msr(0xC001'1023, value);
  }

  // 1091: 4K Address Boundary Crossing Load Operation May Receive Stale Data
  value = msr->read_msr(0xC001'102D);
  value |= (1ull << 34);
  msr->write_msr(0xC001'102D, value);

  // 1095: Potential Violation of Read Ordering In Lock Operation in SMT Mode
  // TODO(fxbug.dev/37450): Do not apply this workaround if SMT is disabled.
  value = msr->read_msr(X86_MSR_AMD_LS_CFG);
  value |= (1ull << 57);
  msr->write_msr(X86_MSR_AMD_LS_CFG, value);
}

void x86_amd_cpu_set_turbo(const cpu_id::CpuId* cpu, MsrAccess* msr, Turbostate state) {
  if (cpu->ReadFeatures().HasFeature(cpu_id::Features::HYPERVISOR)) {
    return;
  }
  if (!cpu->ReadFeatures().HasFeature(cpu_id::Features::CPB)) {
    return;
  }

  uint64_t value = msr->read_msr(/*msr_index=*/X86_MSR_K7_HWCR);
  uint64_t new_value = value;
  switch (state) {
    case Turbostate::ENABLED:
      new_value &= ~(X86_MSR_K7_HWCR_CPB_DISABLE);
      break;
    case Turbostate::DISABLED:
      new_value |= (X86_MSR_K7_HWCR_CPB_DISABLE);
      break;
  }
  if (new_value != value) {
    msr->write_msr(/*msr_index=*/X86_MSR_K7_HWCR, /*value=*/new_value);
  }
}

void x86_amd_init_percpu(void) {
  cpu_id::CpuId cpuid;
  MsrAccess msr;

  x86_amd_set_lfence_serializing(&cpuid, &msr);

  // Quirks
  if (!x86_feature_test(X86_FEATURE_HYPERVISOR)) {
    auto processor_id = cpuid.ReadProcessorId();
    switch (processor_id.family()) {
      case 0x17:
        if (processor_id.model() > 0x0 && processor_id.model() <= 0xF) {
          x86_amd_init_percpu_17h_zen1_quirks(&cpuid, &msr);
        }
        break;
    }
  }
}
