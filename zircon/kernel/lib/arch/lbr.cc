// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/lbr.h>

namespace arch {

uint64_t LbrFromIpMsr::ip(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
      return modern_ip();
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return legacy_without_tsx_ip();
    case X86LbrFormat::k64BitEipWithFlagsTsx:
      return legacy_with_tsx_ip();
  };
  return 0;
}

std::optional<bool> LbrFromIpMsr::tsx_abort(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return {};
    case X86LbrFormat::k64BitEipWithFlagsTsx:
      return legacy_tsx_abort();
  };
  return {};
}

std::optional<bool> LbrFromIpMsr::in_tsx(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return {};
    case X86LbrFormat::k64BitEipWithFlagsTsx:
      return legacy_in_tsx();
  }
  return {};
}

std::optional<bool> LbrFromIpMsr::mispredicted(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
      return {};
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitEipWithFlagsTsx:
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return legacy_mispredicted();
  }
  return {};
}

uint64_t LbrToIpMsr::ip(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitEipWithFlagsTsx:
      return modern_ip();
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return legacy_ip();
  }
  return 0;
}

std::optional<uint16_t> LbrToIpMsr::cycle_count(X86LbrFormat format) const {
  switch (format) {
    case X86LbrFormat::k32Bit:
    case X86LbrFormat::k64BitLip:
    case X86LbrFormat::k64BitEip:
    case X86LbrFormat::k64BitEipWithInfo:
    case X86LbrFormat::k64BitLipWithInfo:
    case X86LbrFormat::k64BitEipWithFlags:
    case X86LbrFormat::k64BitEipWithFlagsTsx:
      return {};
    case X86LbrFormat::k64BitLipWithFlagsCycles:
      return legacy_cycle_count();
  }
  return {};
}

size_t LbrStack::Size(Microarchitecture microarch) {
  // [intel/vol3]: Table 17-4.  LBR Stack Size and TOS Pointer Range.
  switch (microarch) {
    case Microarchitecture::kUnknown:
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
      return 0;
    case Microarchitecture::kIntelCore2:
      return 4;
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
      return 8;
    case Microarchitecture::kIntelNehalem:
    case Microarchitecture::kIntelWestmere:
    case Microarchitecture::kIntelSandyBridge:
    case Microarchitecture::kIntelIvyBridge:
    case Microarchitecture::kIntelHaswell:
    case Microarchitecture::kIntelBroadwell:
      return 16;
    case Microarchitecture::kIntelSkylake:
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
      return 32;
  }
  return 0;
}

bool LbrStack::SupportsCallstackProfiling(Microarchitecture microarch) {
  // Gleaned from scouring [intel/v4] to see which microarchitectures have
  // MSR_LBR_SELECT.EN_CALLSTACK defined.
  switch (microarch) {
    case Microarchitecture::kUnknown:
    case Microarchitecture::kIntelCore2:
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont:
    case Microarchitecture::kIntelNehalem:
    case Microarchitecture::kIntelWestmere:
    case Microarchitecture::kIntelSandyBridge:
    case Microarchitecture::kIntelIvyBridge:
    case Microarchitecture::kAmdFamilyBulldozer:
    case Microarchitecture::kAmdFamilyJaguar:
    case Microarchitecture::kAmdFamilyZen:
    case Microarchitecture::kAmdFamilyZen3:
      return false;
    case Microarchitecture::kIntelHaswell:
    case Microarchitecture::kIntelBroadwell:
    case Microarchitecture::kIntelSkylake:
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont:
      return true;
  }
  return false;
}

LbrSelectMsr LbrStack::GetDefaultSettings(bool for_user) const {
  // Confusingly, setting MSR_LBR_SELECT.CPL_EQ_0 means that branches ending
  // in ring 0 are *discarded*; similarly, setting CPL_NEQ_0 means that
  // branches ending in ring > 0 are.
  //
  // Capture conditional branches, and near indirect and relative jumps;
  // disable capture of near returns, and near indirect and relative calls,
  // which is information already deducible from a backtrace.
  auto select = LbrSelectMsr::Get()
                    .FromValue(0)
                    .set_cpl_eq_0(unsigned{for_user})
                    .set_cpl_neq_0(unsigned{!for_user})
                    .set_jcc(1)
                    .set_near_ind_jmp(1)
                    .set_near_rel_jmp(1)
                    .set_near_ind_call(0)
                    .set_near_rel_call(0)
                    .set_near_ret(0);
  // Enable the callstack profiling mode if supported.
  return callstack_profiling_ ? select.set_en_callstack(1) : select;
}

}  // namespace arch
