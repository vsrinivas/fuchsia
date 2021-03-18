// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_EXTENSION_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_EXTENSION_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/feature.h>
#include <lib/arch/x86/msr.h>

namespace arch {

// [intel/vol4]: Table 2-2.  IA-32 Architectural MSRs (Contd.).
//
// IA32_TSX_CTRL.
//
// TSX (Transactional Synchronization Extension) controls.
struct TsxControlMsr : public X86MsrBase<TsxControlMsr, X86Msr::IA32_TSX_CTRL> {
  // Bits [63:2] are reserved.
  DEF_BIT(1, rtm_disable);
  DEF_BIT(0, tsx_cpuid_clear);

  template <typename CpuidIoProvider, typename MsrIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
    return ArchCapabilitiesMsr::IsSupported(cpuid) &&
           ArchCapabilitiesMsr::Get().ReadFrom(&msr).tsx_ctrl();
  }
};

template <typename CpuidIoProvider>
inline bool TsxIsSupported(CpuidIoProvider&& cpuid) {
  // [intel/vol3]: 18.3.6.5     Performance Monitoring and Intel® TSX.
  const auto features = cpuid.template Read<CpuidExtendedFeatureFlagsB>();
  return features.hle() || features.rtm();
}

// Attempts to disable TSX and returns whether it was successful.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool DisableTsx(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  if (!TsxControlMsr::IsSupported(cpuid, msr)) {
    return false;
  }
  TsxControlMsr::Get().ReadFrom(&msr).set_rtm_disable(1).set_tsx_cpuid_clear(1).WriteTo(&msr);
  return true;
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_EXTENSION_H_
