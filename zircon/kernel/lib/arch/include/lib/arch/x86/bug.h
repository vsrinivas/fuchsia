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
#include <lib/arch/x86/speculation.h>

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
//
// For non-architectural MSRs whose fields give workarounds to a specific
// erratum, if no further qualification is given (e.g., a field name/mnemonic),
// we name the field "erratum_${ID}_workaround".

namespace arch {

namespace internal {

// A trivial MSR I/O provider that can be used in instances in which we do not
// wish MSR writes to take effect but still wish to observe the result (e.g., a
// dry-run context of sorts).
struct NullMsrIo {
  template <typename IntType>
  void Write(IntType value, uint32_t msr) const {}

  template <typename IntType>
  IntType Read(uint32_t msr) const {
    return 0;
  }
};

}  // namespace internal

// [amd/ssbd] references bits 10, 33, 54.
// [amd/rg/17h/00h-0Fh] references bits 4, 57.
//
// MSRC001_1020.
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

// [amd/rg/17h/00h-0Fh] references bit 4.
//
// MSRC001_1028.
struct AmdC0011028Msr : public X86MsrBase<AmdC0011028Msr, X86Msr::MSRC001_1028> {
  // Bits [63:5] are reserved/unknown.
  DEF_BIT(4, erratum_1049_workaround);
  // Bits [3:0] are reserved/unknown.
};

// [amd/rg/17h/00h-0Fh] references bit 13.
//
// MSRC001_1029.
struct AmdC0011029Msr : public X86MsrBase<AmdC0011029Msr, X86Msr::MSRC001_1029> {
  // Bits [63:14] are reserved/unknown.
  DEF_BIT(13, erratum_1021_workaround);
  // Bits [12:0] are reserved/unknown.
};

// [amd/rg/17h/00h-0Fh] references bit 34.
//
// MSRC001_102D.
struct AmdC001102dMsr : public X86MsrBase<AmdC001102dMsr, X86Msr::MSRC001_102D> {
  // Bits [63:35] are reserved/unknown.
  DEF_BIT(34, erratum_1091_workaround);
  // Bits [33:0] are reserved/unknown.
};

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
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
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
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
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

// Whether the CPU is susceptible to the Speculative Store Bypass (SSB) bug:
// https://software.intel.com/security-software-guidance/advisory-guidance/speculative-store-bypass
//
// CVE-2018-3639.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86SsbBug(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  // Check if the processor explicitly advertises that it is not affected, in
  // both the Intel and AMD ways.
  if (ArchCapabilitiesMsr::IsSupported(cpuid) &&
      ArchCapabilitiesMsr::Get().ReadFrom(&msr).ssb_no()) {
    return false;
  }
  if (CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid) &&
      cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>().ssb_no()) {
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
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
      return true;
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
      break;
  }
  return false;
}

// Attempt to mitigate the SSB bug. Return true if the bug was successfully
// mitigated.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool MitigateX86SsbBug(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  if (cpuid.template Read<CpuidExtendedFeatureFlagsD>().ssbd()) {
    ZX_DEBUG_ASSERT(SpeculationControlMsr::IsSupported(cpuid));
    SpeculationControlMsr::Get().ReadFrom(&msr).set_ssbd(1).WriteTo(&msr);
    return true;
  }

  if (CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid)) {
    const auto amd_features = cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>();
    if (amd_features.ssbd()) {
      ZX_DEBUG_ASSERT(SpeculationControlMsr::IsSupported(cpuid));
      SpeculationControlMsr::Get().ReadFrom(&msr).set_ssbd(1).WriteTo(&msr);
      return true;
    }

    if (amd_features.virt_ssbd()) {
      ZX_DEBUG_ASSERT(AmdVirtualSpeculationControlMsr::IsSupported(cpuid));
      AmdVirtualSpeculationControlMsr::Get().ReadFrom(&msr).set_ssbd(1).WriteTo(&msr);
      return true;
    }
  }

  // [amd/ssbd]: NON-ARCHITECTURAL MSRS.
  //
  // There are non-architectural mechanisms to disable SSB for AMD families
  // 0x15-0x17.
  switch (GetMicroarchitecture(cpuid)) {
    case arch::Microarchitecture::kAmdFamilyBulldozer:
      AmdLoadStoreConfigurationMsr::Get().ReadFrom(&msr).set_ssbd_15h(1).WriteTo(&msr);
      return true;
    case arch::Microarchitecture::kAmdFamilyJaguar:
      AmdLoadStoreConfigurationMsr::Get().ReadFrom(&msr).set_ssbd_16h(1).WriteTo(&msr);
      return true;
    case arch::Microarchitecture::kAmdFamilyZen:
      AmdLoadStoreConfigurationMsr::Get().ReadFrom(&msr).set_ssbd_17h(1).WriteTo(&msr);
      return true;
    default:
      break;
  }

  return false;
}

template <typename CpuidIoProvider>
inline bool CanMitigateX86SsbBug(CpuidIoProvider&& cpuid) {
  // With a null I/O provider, we can make the requisite checks without
  // actually committing the writes.
  return MitigateX86SsbBug(cpuid, internal::NullMsrIo{});
}

// Whether the CPU is susceptible to the Rogue Data Cache Load (Meltdown) bug:
// https://software.intel.com/security-software-guidance/advisory-guidance/rogue-data-cache-load.
//
// CVE-2017-5754.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86MeltdownBug(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  // Check if the processor explicitly advertises that it is not affected.
  if (ArchCapabilitiesMsr::IsSupported(cpuid) &&
      ArchCapabilitiesMsr::Get().ReadFrom(&msr).rdcl_no()) {
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
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
      return true;
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
      break;
  }
  return false;
}

// Whether the CPU is susceptible to the L1 Terminal Fault (L1TF) bug:
// https://software.intel.com/security-software-guidance/advisory-guidance/l1-terminal-fault.
//
// CVE-2018-3615, CVE-2018-3620, CVE-2018-3646.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline bool HasX86L1tfBug(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  // Advertisement of RDCL_NO also implies non-susceptibility to L1TF:
  // https://software.intel.com/security-software-guidance/deep-dives/deep-dive-intel-analysis-l1-terminal-fault.
  if (ArchCapabilitiesMsr::IsSupported(cpuid) &&
      ArchCapabilitiesMsr::Get().ReadFrom(&msr).rdcl_no()) {
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
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelBonnell:
      return true;
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
      break;
  }
  return false;
}

// An architecturally prescribed mitigation for Spectre v2.
enum class SpectreV2Mitigation {
  // Enhanced/always-on IBRS (i.e., IBRS that can be enabled once without
  // automatic disabling) is preferred alone.
  kIbrs,

  // IBPB and/or retpoline are recommended.
  kIbpbRetpoline,

  // IBPB and/or retpoline are recommended - and STIPB, though not preferred as
  // a performant mitigation, is also present and may be used.
  kIbpbRetpolineStibp,
};

// Returns the preferred Spectre v2 mitigation strategy.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline SpectreV2Mitigation GetPreferredSpectreV2Mitigation(CpuidIoProvider&& cpuid,
                                                           MsrIoProvider&& msr) {
  switch (GetVendor(cpuid)) {
    case Vendor::kUnknown:
      break;
    case Vendor::kIntel:
      // https://software.intel.com/security-software-guidance/advisory-guidance/branch-target-injection
      //
      // If enhanced IBRS are supported, it should be used for mitigation
      // instead of retpoline; else retpoline
      if (HasIbrs(cpuid, msr, /*always_on_mode=*/true)) {
        return SpectreV2Mitigation::kIbrs;
      }
      break;
    case Vendor::kAmd: {
      // [amd/ibc]: EXTENDED USAGE MODELS.
      // AMD further offers a feature bit to indicate whether IBRS is a
      // preffered mitigation strategy.
      if (HasIbrs(cpuid, msr, /*always_on_mode=*/true) &&
          CpuidSupports<CpuidExtendedAmdFeatureFlagsB>(cpuid) &&
          cpuid.template Read<CpuidExtendedAmdFeatureFlagsB>().prefers_ibrs()) {
        return SpectreV2Mitigation::kIbrs;
      }
      // [amd/ibc]: USAGE.
      // Though not recommended, STIPB is still a viable mitigation strategy.
      if (HasStibp(cpuid, /*always_on_mode=*/true)) {
        return SpectreV2Mitigation::kIbpbRetpolineStibp;
      }
      break;
    }
  }

  // Retpolines comprise an architecturally agnostic, pure software solution,
  // which makes it a sensible default strategy.
  return SpectreV2Mitigation::kIbpbRetpoline;
}

// Applies workarounds to processor-specific errata.
template <typename CpuidIoProvider, typename MsrIoProvider>
inline void ApplyX86ErrataWorkarounds(CpuidIoProvider&& cpuid, MsrIoProvider&& msr) {
  if (cpuid.template Read<CpuidFeatureFlagsC>().hypervisor()) {
    return;
  }

  switch (arch::GetVendor(cpuid)) {
    case Vendor::kUnknown:
      break;
    case Vendor::kIntel:
      break;
    case Vendor::kAmd: {
      const auto info = cpuid.template Read<CpuidVersionInfo>();
      switch (info.family()) {
        case 0x17: {
          switch (info.model()) {
            // [amd/rg/17h/00h-0Fh].
            case 0x00 ... 0x0f: {
              // ZP-B1 refers to (model, stepping) == (1, 1); some of the errata
              // are detailed as only applying to that CPU.
              const bool zp_b1 = info.model() == 1 && info.stepping() == 1;

              // 1021: Load Operation May Receive Stale Data From Older Store Operation.
              AmdC0011029Msr::Get().ReadFrom(&msr).set_erratum_1021_workaround(1).WriteTo(&msr);

              auto lscfg = AmdLoadStoreConfigurationMsr::Get().ReadFrom(&msr);
              // 1033: A Lock Operation May Cause the System to Hang.
              if (zp_b1) {
                lscfg.set_erratum_1033_workaround(1);
              }
              // 1095: Potential Violation of Read Ordering In Lock Operation in SMT Mode.
              if (true) {  // TODO(fxbug.dev/37450): Do not apply if SMT is disabled.
                lscfg.set_erratum_1095_workaround(1);
              }
              lscfg.WriteTo(&msr);

              // 1049: FCMOV Instruction May Not Execute Correctly.
              AmdC0011028Msr::Get().ReadFrom(&msr).set_erratum_1049_workaround(1).WriteTo(&msr);

              // 1091: 4K Address Boundary Crossing Load Operation May Receive Stale Data.
              AmdC001102dMsr::Get().ReadFrom(&msr).set_erratum_1091_workaround(1).WriteTo(&msr);
              break;
            }
          }
          break;
        }
      }
      break;
    }
  }
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_BUG_H_
