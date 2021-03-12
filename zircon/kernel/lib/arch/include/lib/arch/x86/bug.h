// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/arch/x86/extension.h>
#include <lib/arch/x86/feature.h>

// This file contains utilities related to probing and mitigating architectural
// bugs and vulnerabilities.
//
// In general, we cannot rely on the official means of enumerating whether a
// vulnerability is present. For example, it might only be enumerable after
// certain microcode updates are performed. Accordingly, if we cannot get an
// definitive "is not vulnerable" from the official means, we fall back to
// pessimistically assigning vulnerability on the basis of microarchitecture,
// making implicit reference to the following documents:
//
// * Intel:
//   https://software.intel.com/security-software-guidance/processors-affected-transient-execution-attack-mitigation-product-cpu-model
//   Discontinued models (e.g., Core 2, Nehalem, and Westmere) are not present
//   in the table; in those cases, we assume vulnerability by default, unless
//   otherwise mentions.
//
// * AMD: https://www.amd.com/en/corporate/product-security
//
// Further pessimistically, we default to assigning vulnerability in the case
// unknown architectures.

namespace arch {

// Whether the CPU is susceptible to swapgs speculation attacks:
// https://software.intel.com/security-software-guidance/advisory-guidance/speculative-behavior-swapgs-and-segment-registers
//
// CVE-2019-1125.
template <typename CpuidIoProvider>
inline bool HasX86SwapgsBug(CpuidIoProvider&& cpuid) {
  switch (arch::GetVendor(cpuid)) {
    case arch::Vendor::kUnknown:
    // All Intel CPUs seem to be affected and there is no indication that they
    // intend to fix this.
    case arch::Vendor::kIntel:
      return true;
    case arch::Vendor::kAmd:
      return false;
  }
  return false;
}

// Whether the CPU is susceptible to any of the Microarchitectural Data
// Sampling (MDS) bugs.
//
// CVE-2018-12126, CVE-2018-12127, CVE-2018-12130, CVE-2019-11091.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86MdsBugs(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  // https://software.intel.com/security-software-guidance/resources/processors-affected-microarchitectural-data-sampling
  if (ArchCapabilitiesMsr::IsSupported(cpuid) &&
      ArchCapabilitiesMsr::Get().ReadFrom(&msr).mds_no()) {
    return false;
  }

  switch (GetMicroarchitecture(cpuid)) {
    case Microarchitecture::kUnknown:
    case Microarchitecture::kIntelCore2:
    case Microarchitecture::kIntelNehalem:
    case Microarchitecture::kIntelWestmere:
    case Microarchitecture::kIntelSandyBridge:
    case Microarchitecture::kIntelIvyBridge:
    case Microarchitecture::kIntelHaswell:
    case Microarchitecture::kIntelBroadwell:
    case Microarchitecture::kIntelSkylake:
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
      return true;
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
    case Microarchitecture::kAmdFamily0x15:
    case Microarchitecture::kAmdFamily0x16:
    case Microarchitecture::kAmdFamily0x17:
    case Microarchitecture::kAmdFamily0x19:
      return false;
  }
  return true;
}

// Whether the CPU is susceptible to the TSX Asynchronous Abort (TAA) bug.
//
// CVE-2019-11135.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86TaaBug(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  // https://software.intel.com/security-software-guidance/advisory-guidance/intel-transactional-synchronization-extensions-intel-tsx-asynchronous-abort
  //
  // A processor is affected by TAA if both of the following are true:
  // * CPU supports TSX (indicated by the HLE or RTM features);
  // * CPU does not enumerate TAA_NO.
  const bool taa_no =
      ArchCapabilitiesMsr::IsSupported(cpuid) && ArchCapabilitiesMsr::Get().ReadFrom(&msr).taa_no();
  if (!TsxIsSupported(cpuid) || taa_no) {
    return false;
  }
  switch (GetMicroarchitecture(cpuid)) {
    case Microarchitecture::kUnknown:
    case Microarchitecture::kIntelHaswell:
    case Microarchitecture::kIntelBroadwell:
    case Microarchitecture::kIntelSkylake:
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelCannonLake:
      return true;
    case Microarchitecture::kIntelCore2:     // Does not implement TSX.
    case Microarchitecture::kIntelNehalem:   // Does not implement TSX.
    case Microarchitecture::kIntelWestmere:  // Does not implement TSX.
    case Microarchitecture::kIntelSandyBridge:
    case Microarchitecture::kIntelIvyBridge:
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
    case Microarchitecture::kAmdFamily0x15:
    case Microarchitecture::kAmdFamily0x16:
    case Microarchitecture::kAmdFamily0x17:
    case Microarchitecture::kAmdFamily0x19:
      return false;
  }
  return true;
}

// Whether the CPU is susceptible to any of the MDS or TAA bugs, which are
// closely related and similarly mitigated.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86MdsTaaBugs(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  return HasX86MdsBugs(cpuid, msr) || HasX86TaaBug(cpuid, msr);
}

// Whether the MDS/TAA bugs can be mitigated, which all make use of the same
// method (MD_CLEAR):
// https://software.intel.com/security-software-guidance/deep-dives/deep-dive-intel-analysis-microarchitectural-data-sampling#mitigation4processors
template <typename CpuidIoProvider>
inline bool CanMitigateX86MdsTaaBugs(CpuidIoProvider&& cpuid) {
  return cpuid.template Read<CpuidExtendedFeatureFlagsD>().md_clear();
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_
