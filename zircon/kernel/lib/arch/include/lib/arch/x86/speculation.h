// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/msr.h>

#include <hwreg/bitfields.h>

namespace arch {

// [intel/vol4]: Table 2-2.  IA-32 Architectural MSRs (Contd.).
// [amd/ibc]: PRESENCE.
// [amd/ssbd]: PRESENCE.
//
// IA32_SPEC_CTRL.
//
// Speculation control.
struct SpeculationControlMsr : public hwreg::RegisterBase<SpeculationControlMsr, uint64_t> {
  // Bits [63:3] are reserved.
  DEF_BIT(2, ssbd);
  DEF_BIT(1, stibp);
  DEF_BIT(0, ibrs);

  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    // Intel documents that the MSR is supported only if one of the kinds of
    // speculation it can control is itself enumerated; AMD does similarly, but
    // that information must be cobbled together from [amd/ibc] and [amd/ssbd].

    // The Intel way:
    const auto intel_features = cpuid.template Read<CpuidExtendedFeatureFlagsD>();
    if (intel_features.ibrs_ibpb() || intel_features.stibp() || intel_features.ssbd()) {
      return true;
    }

    // The AMD way:
    if (!CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid)) {
      return false;
    }
    const auto amd_features = cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>();
    return amd_features.ibrs() || amd_features.stibp() || amd_features.ssbd();
  }

  static auto Get() {
    return hwreg::RegisterAddr<SpeculationControlMsr>(
        static_cast<uint32_t>(X86Msr::IA32_SPEC_CTRL));
  }
};

// [amd/ssbd]: PRESENCE.
//
// MSR_VIRT_SPEC_CTRL.
//
// Virtual speculation control (e.g., for hypervisor usage).
struct AmdVirtualSpeculationControlMsr
    : public hwreg::RegisterBase<AmdVirtualSpeculationControlMsr, uint64_t> {
  // Bits [63:3] are reserved.
  DEF_BIT(2, ssbd);
  // Bits [1:0] are reserved.

  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    // [amd/ssbd]: HYPERVISOR USAGE MODELS.
    return CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid) &&
           cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>().virt_ssbd();
  }

  static auto Get() {
    return hwreg::RegisterAddr<AmdVirtualSpeculationControlMsr>(
        static_cast<uint32_t>(X86Msr::MSR_VIRT_SPEC_CTRL));
  }
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_
