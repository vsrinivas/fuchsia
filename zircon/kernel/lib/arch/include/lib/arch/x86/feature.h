// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_

#include <lib/arch/hwreg.h>
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

// [intel/vol4]: Table 2-3.  MSRs in Processors Based on Intel® Core™ Microarchitecture.
//
// IA32_MISC_ENABLE.
//
// Enables miscellaenous processor features.
struct MiscFeaturesMsr : public X86MsrBase<MiscFeaturesMsr, X86Msr::IA32_MISC_ENABLE> {
  // Bits [63:40] are reserved.
  DEF_BIT(39, ip_prefetch_disable);
  DEF_BIT(38, ida_disable);
  DEF_BIT(37, dcu_prefetch_disable);
  // Bits [36:35] are reserved.
  DEF_BIT(34, xd_bit_disable);
  // Bits [33:24] are reserved.
  DEF_BIT(23, xtpr_message_disable);
  DEF_BIT(22, limit_cpuid_maxval);
  // Bit 21 is reserved.
  DEF_BIT(20, eist_select_lock);
  DEF_BIT(19, adjacent_cache_line_prefetch_disable);
  DEF_BIT(18, monitor_fsm);
  // Bit 17 is reserved.
  DEF_BIT(16, eist);
  // Bits [15:14] are reserved.
  DEF_BIT(13, tm2);
  DEF_BIT(12, pebs_unavailable);
  DEF_BIT(11, bts_unavailable);
  DEF_BIT(10, ferr_mux);
  DEF_BIT(9, hardware_prefetch_disable);
  // Bit 8 is reserved.
  DEF_BIT(7, perf_mon_available);
  // Bits [6:4] are reserved.
  DEF_BIT(3, automatic_thermal_control_circuit);
  // Bits [2:1] are reserved.
  DEF_BIT(0, fast_strings);

  template <typename CpuidIoProvider>
  static bool IsSupported(CpuidIoProvider&& cpuid) {
    return arch::GetVendor(cpuid) == arch::Vendor::kIntel;
  }
};

// [amd/ppr/17h/01h,08h]:  2.1.14.2 MSRs - MSRC000_0xxx.
//
// MSRC001_0015.
//
// AMD hardware configuration.
struct AmdHardwareConfigurationMsr
    : public X86MsrBase<AmdHardwareConfigurationMsr, X86Msr::MSRC001_0015> {
  // Bits [63:31] are reserved.
  DEF_BIT(30, ir_perf_en);
  // Bits [29:28] are reserved,
  DEF_BIT(27, eff_freq_read_only_lock);
  DEF_BIT(26, eff_frq_cnt_mwait);
  DEF_BIT(25, cpb_dis);
  DEF_BIT(24, tsc_freq_sel);
  // Bits [23:22] are reserved.
  DEF_BIT(21, lock_tsc_to_current_p0);
  DEF_BIT(20, io_cfg_gp_fault);
  // Bit 19 is reserved.
  DEF_BIT(18, mc_status_wr_en);
  DEF_BIT(17, wrap32_dis);
  // Bits [16:15] are reserved.
  DEF_BIT(14, rsm_sp_cyc_dis);
  DEF_BIT(13, smi_sp_cyc_dis);
  // Bits [12:11] are reserved.
  DEF_BIT(10, mon_mwait_user_en);
  DEF_BIT(9, mon_mwait_dis);
  DEF_BIT(8, ignne_em);
  DEF_BIT(7, allow_ferr_on_ne);
  // Bits [6:5] are reserved.
  DEF_BIT(4, invdwbinvd);
  DEF_BIT(3, tlb_cache_dis);
  // Bits [2:1] are reserved.
  DEF_BIT(0, smm_lock);
};

// [intel/vol3]: 2.2.1 Extended Feature Enable Register
// [amd/vol2]: 3.1.7 Extended Feature Enable Register (EFER)
//
// IA32_EFER
//
struct X86ExtendedFeatureEnableRegisterMsr
    : public X86MsrBase<X86ExtendedFeatureEnableRegisterMsr, X86Msr::IA32_EFER> {
  DEF_RSVDZ_FIELD(63, 19);
  // Bits [18:12] are reserved in Intel docs, but further specified by AMD.
  // AMD documents the reserved bits among [63:9] as MBZ while Intel simply
  // says "reserved".

  DEF_BIT(17, mcommit);  // (AMD only) Enable mcommit instruction.
  DEF_RSVDZ_BIT(16);     // Reserved, MBZ in AMD.
  DEF_BIT(15, tce);      // (AMD only) Translation Cache Extension.
  DEF_BIT(14, ffxsr);    // (AMD only) Fast fxsave/fxrstor.
  DEF_BIT(13, lmsle);    // (AMD only) Long Mode Segment Limit Enable
  DEF_BIT(12, svme);     // (AMD only) Secure Virtual Machine Enable
  DEF_BIT(11, nxe);      // Enable non-execute bit in page tables.
  DEF_BIT(10, lma);      // IA-32e (x86-64) mode active.
  DEF_RSVDZ_BIT(9);      // Reserved, MBZ in AMD.
  DEF_BIT(8, lme);       // IA-32e (x86-64) mode enable.

  // Bits [7:1] are reserved.  AMD documents them as R(ead)A(s)Z(ero) while
  // Intel simply says "reserved".

  DEF_BIT(0, sce);  // Enable syscall/sysret instructions.
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_FEATURE_H_
