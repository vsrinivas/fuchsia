// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_TRACE_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_TRACE_H_

#include <lib/arch/x86/msr.h>

#include <optional>

#include <hwreg/bitfields.h>

namespace arch {

// [intel/vol3]: 17.4.1 IA32_DEBUGCTL MSR.
// [amd/vol2]: 13.1.1.6  Debug-Control MSR (DebugCtl).
//
// Trace/Profile Resource Control.
struct DebugControlMsr : public hwreg::RegisterBase<DebugControlMsr, uint64_t> {
  // Bits [63:16] are reserved.
  DEF_BIT(15, rtm_debug);
  DEF_BIT(14, freeze_while_smm);
  DEF_BIT(13, enable_uncore_pmi);
  DEF_BIT(12, freeze_perfmon_on_pmi);
  DEF_BIT(11, freeze_lbr_on_pmi);
  DEF_BIT(10, bts_off_usr);
  DEF_BIT(9, bts_off_os);
  DEF_BIT(8, btint);
  DEF_BIT(7, bts);
  DEF_BIT(6, tr);
  // Bits [5:2] are reserved.
  DEF_BIT(1, btf);
  DEF_BIT(0, lbr);

  static auto Get() {
    return hwreg::RegisterAddr<DebugControlMsr>(static_cast<uint32_t>(X86Msr::IA32_DEBUGCTL));
  }
};

// [intel/vol3]: 17.4.8.1  LBR Stack and Intel® 64 Processors.
//  `
// Last Branch Record format.
enum class LbrFormat : uint8_t {
  k32Bit = 0b000000,
  k64BitLip = 0b000001,
  k64BitEip = 0b000010,
  k64BitEipWithFlags = 0b000011,
  k64BitEipWithFlagsTsx = 0b000100,
  k64BitEipWithFlagsInfo = 0b000101,
  k64BitLipWithFlagsCycles = 0b000110,
  k64BitLipWithFlagsInfo = 0b000111,
};

// [intel/vol3]; 18.6.2.4.2  PEBS Record Format.
//
// PEBS record format.
enum class PebsFormat : uint8_t {
  k0000B = 0b0000,
  k0001B = 0b0001,
  k0010B = 0b0010,
  k0011B = 0b0011,
  k0100B = 0b0100,
};

// [intel/vol3]: Figure 18-63.  Layout of IA32_PERF_CAPABILITIES MSR.
//
// Enumerates the existence of performance monitoring features.
struct PerfCapabilitiesMsr : public hwreg::RegisterBase<PerfCapabilitiesMsr, uint64_t> {
  // Bits [63: 17] are reserved.
  DEF_BIT(16, pebs_output_pt_avail);
  DEF_BIT(15, perf_metrics_available);
  // Bit 14 is reserved.
  DEF_BIT(13, fw_write);
  DEF_BIT(12, smm_freeze);
  DEF_ENUM_FIELD(PebsFormat, 11, 8, pebs_rec_fmt);
  DEF_BIT(7, pebs_arch_reg);
  DEF_BIT(6, pebs_trap);
  DEF_ENUM_FIELD(LbrFormat, 5, 0, lbr_fmt);

  static auto Get() {
    return hwreg::RegisterAddr<PerfCapabilitiesMsr>(
        static_cast<uint32_t>(X86Msr::IA32_PERF_CAPABILITIES));
  }
};

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_X86_TRACE_H_
