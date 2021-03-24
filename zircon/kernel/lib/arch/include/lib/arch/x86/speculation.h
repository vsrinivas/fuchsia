// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/feature.h>
#include <lib/arch/x86/msr.h>
#include <zircon/assert.h>

namespace arch {

// [intel/vol4]: Table 2-2.  IA-32 Architectural MSRs (Contd.).
// [amd/ibc]: PRESENCE.
// [amd/ssbd]: PRESENCE.
//
// IA32_SPEC_CTRL.
//
// Speculation control.
struct SpeculationControlMsr : public X86MsrBase<SpeculationControlMsr, X86Msr::IA32_SPEC_CTRL> {
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
};

// [amd/ssbd]: PRESENCE.
//
// MSR_VIRT_SPEC_CTRL.
//
// Virtual speculation control (e.g., for hypervisor usage).
struct AmdVirtualSpeculationControlMsr
    : public X86MsrBase<AmdVirtualSpeculationControlMsr, X86Msr::MSR_VIRT_SPEC_CTRL> {
  // Bits [63:3] are reserved.
  DEF_BIT(2, ssbd);
  // Bits [1:0] are reserved.

  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    // [amd/ssbd]: HYPERVISOR USAGE MODELS.
    return CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid) &&
           cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>().virt_ssbd();
  }
};

// [intel/vol4]: Table 2-2.  IA-32 Architectural MSRs (Contd.).
// [amd/ibc]: PRESENCE.
//
// IA32_PRED_CMD.
//
// Prediction command control (write-only).
struct PredictionCommandMsr : public X86MsrBase<PredictionCommandMsr, X86Msr::IA32_PRED_CMD> {
  // Bits [63:1] are reserved.
  DEF_BIT(0, ibpb);

  // This MSR is supported if any of its corresponding features are in turn
  // supported.
  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    return HasIbpb(cpuid);
  }
};

// Whether the Indirect Branch Prediction Barrier (IBPB) is supported.
//
// https://software.intel.com/security-software-guidance/deep-dives/deep-dive-indirect-branch-predictor-barrier.
template <typename CpuidIoProvider>
inline bool HasIbpb(CpuidIoProvider&& cpuid) {
  // The Intel way.
  if (CpuidSupports<CpuidExtendedFeatureFlagsD>(cpuid) &&
      cpuid.template Read<CpuidExtendedFeatureFlagsD>().ibrs_ibpb()) {
    return true;
  }

  // [amd/ibc]: PRESENCE.
  // The AMD way.
  return CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid) &&
         cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>().ibpb();
}

// Issues an IBPB (Indirect Branch Prediction Barrier), which requires the
// feature to be supported.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline void IssueIbpb(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  ZX_DEBUG_ASSERT(arch::PredictionCommandMsr::IsSupported(cpuid));
  arch::PredictionCommandMsr::Get().FromValue(0).set_ibpb(1).WriteTo(&msr);
}

// Whether Indirect Branch Restricted Speculation (IBRS) is supported. The
// "always on" mode refers to an optimization in which IBRS need only be
// enabled once; IBRS in this mode are also referred to as "enhanced".
//
// https://software.intel.com/security-software-guidance/deep-dives/deep-dive-indirect-branch-restricted-speculation.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasIbrs(CpuidIoProvider&& cpuid, MsrIoProvider&& msr, bool always_on_mode) {
  // The Intel way.
  const bool intel_always_on = ArchCapabilitiesMsr::IsSupported(cpuid) &&
                               ArchCapabilitiesMsr::Get().ReadFrom(&msr).ibrs_all();
  const bool intel_present = cpuid.template Read<CpuidExtendedFeatureFlagsD>().ibrs_ibpb();
  if (intel_present && (!always_on_mode || intel_always_on)) {
    return true;
  }

  // The AMD way.
  if (CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid)) {
    const auto features = cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>();
    if (features.ibrs() && (!always_on_mode || features.ibrs_always_on())) {
      return true;
    }
  }

  return false;
}

// Enables IBRS, which requires the feature to be supported.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline void EnableIbrs(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  ZX_DEBUG_ASSERT(SpeculationControlMsr::IsSupported(cpuid));
  SpeculationControlMsr::Get().ReadFrom(&msr).set_ibrs(1).WriteTo(&msr);
}

// Whether Single Thread Indirect Branch Predictors (STIBP) are supported. The
// "always on" mode refers to an optimization in which STIBP need only be
// enabled once.
//
// https://software.intel.com/security-software-guidance/deep-dives/deep-dive-single-thread-indirect-branch-predictors.
template <typename CpuidIoProvider>
inline bool HasStibp(CpuidIoProvider&& cpuid, bool always_on_mode) {
  // The Intel way.
  const bool intel_present = cpuid.template Read<CpuidExtendedFeatureFlagsD>().stibp();
  if (intel_present && !always_on_mode) {  // Intel does not offer an "always on" mode.
    return true;
  }

  // The AMD way.
  if (CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid)) {
    const auto features = cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>();
    if (features.stibp() && (!always_on_mode || features.stibp_always_on())) {
      return true;
    }
  }
  return false;
}

// Enables STIBP, which requires the feature to be supported.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline void EnableStibp(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  ZX_DEBUG_ASSERT(SpeculationControlMsr::IsSupported(cpuid));
  SpeculationControlMsr::Get().ReadFrom(&msr).set_stibp(1).WriteTo(&msr);
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_SPECULATION_H_
