// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/msr.h>

namespace arch {

// [intel/vol4]: Table 2-2.  IA-32 Architectural MSRs (Contd.).
//
// IA32_ARCH_CAPABILITIES.
//
// Enumerates general archicturectural features.
struct ArchCapabilitiesMsr
    : public X86MsrBase<ArchCapabilitiesMsr, X86Msr::IA32_ARCH_CAPABILITIES> {
  // Bits [63:9] are reserved.
  DEF_BIT(8, taa_no);
  DEF_BIT(7, tsx_ctrl);
  DEF_BIT(6, if_pschange_mc_no);
  DEF_BIT(5, mds_no);
  DEF_BIT(4, ssb_no);
  DEF_BIT(3, skip_l1dfl_vmentry);
  DEF_BIT(2, rsba);
  DEF_BIT(1, ibrs_all);
  DEF_BIT(0, rdcl_no);

  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    return cpuid.template Read<CpuidExtendedFeatureFlagsD>().ia32_arch_capabilities();
  }
};

// [amd/ssbd] references bits 10, 33, 54.
// [amd/rg/17h/00h-0Fh] references bits 4, 57.
//
// MSRC001_1020.
//
// AMD's load-store configuration MSR. Its fields are disparately documented,
// and lackingly at that. In the revision guide for specific erratum, fields
// are referenced as enabling workarounds, but further qualification is given
// (e.g., a field name/mnemonic); in those cases, we name the field
// "erratum_${number}_workaround".
struct AmdLoadStoreConfigurationMsr
    : public X86MsrBase<AmdLoadStoreConfigurationMsr, X86Msr::MSRC001_1020> {
  // Bits [63:58] are reserved/unknown.
  DEF_BIT(57, erratum_1095_workaround);
  // Bits [56:55] are reserved/unknown.
  DEF_BIT(54, ssbd_15h);
  // Bits [53:34] are reserved/unknown.
  DEF_BIT(33, ssbd_16h);
  // Bits [32:11] are reserved/unknown.
  DEF_BIT(10, ssbd_17h);
  // Bits [9:5] are reserved/unknown.
  DEF_BIT(4, erratum_1033_workaround);
  // Bits [3:0] are reserved/unknown.
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_
