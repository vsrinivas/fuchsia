// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/lbr.h>

#include <fbl/bits.h>
#include <fbl/function.h>

namespace arch {

uint64_t LbrFromIpMsr::value(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
      return reg_value();
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitLipWithFlagsCycles:
      return fbl::ExtractBits<62, 0, uint64_t>(reg_value());
    case LbrFormat::k64BitEipWithFlagsTsx:
      return fbl::ExtractBits<60, 0, uint64_t>(reg_value());
  };
  __UNREACHABLE;
}

std::optional<bool> LbrFromIpMsr::tsx_abort(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitLipWithFlagsCycles:
      return {};
    case LbrFormat::k64BitEipWithFlagsTsx:
      return fbl::ExtractBit<61, bool>(reg_value());
  };
  __UNREACHABLE;
}

std::optional<bool> LbrFromIpMsr::in_tsx(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitLipWithFlagsCycles:
      return {};
    case LbrFormat::k64BitEipWithFlagsTsx:
      return fbl::ExtractBit<62, bool>(reg_value());
  }
  __UNREACHABLE;
}

std::optional<bool> LbrFromIpMsr::mispredicted(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
      return {};
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitEipWithFlagsTsx:
    case LbrFormat::k64BitLipWithFlagsCycles:
      return fbl::ExtractBit<63, bool>(reg_value());
  }
  __UNREACHABLE;
}

uint64_t LbrToIpMsr::value(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitEipWithFlagsTsx:
      return reg_value();
    case LbrFormat::k64BitLipWithFlagsCycles:
      return fbl::ExtractBits<47, 0, uint64_t>(reg_value());
  }
  __UNREACHABLE;
}

std::optional<uint16_t> LbrToIpMsr::cycle_count(LbrFormat format) {
  switch (format) {
    case LbrFormat::k32Bit:
    case LbrFormat::k64BitLip:
    case LbrFormat::k64BitEip:
    case LbrFormat::k64BitEipWithFlagsInfo:
    case LbrFormat::k64BitLipWithFlagsInfo:
    case LbrFormat::k64BitEipWithFlags:
    case LbrFormat::k64BitEipWithFlagsTsx:
      return {};
    case LbrFormat::k64BitLipWithFlagsCycles:
      return fbl::ExtractBits<63, 48, uint16_t>(reg_value());
  }
  __UNREACHABLE;
}

void LbrStack::Initialize(Microarchitecture microarch, bool supports_pdcm) {
  // [intel/vol3]: Table 17-4.  LBR Stack Size and TOS Pointer Range.
  //
  // Sizes synthesized from the above reference; whether callstack profiling is
  // supported is gleaned from scouring v4 to see which architectures have
  // MSR_LBR_SELECT.EN_CALLSTACK defined.
  //
  // Do not rely on a default case; build failures are a desired failsafe in
  // ensuring that this logic is updated on the introduction of new
  // microarchitectures.
  switch (microarch) {
    case Microarchitecture::kIntelCore2: {
      size_ = 4;
      break;
    }
    case Microarchitecture::kIntelBonnell:
    case Microarchitecture::kIntelSilvermont:
    case Microarchitecture::kIntelAirmont: {
      size_ = 8;
      break;
    }
    case Microarchitecture::kIntelNehalem:
    case Microarchitecture::kIntelWestmere:
    case Microarchitecture::kIntelSandyBridge:
    case Microarchitecture::kIntelIvyBridge: {
      size_ = 16;
      break;
    }
    case Microarchitecture::kIntelHaswell:
    case Microarchitecture::kIntelBroadwell: {
      size_ = 16;
      callstack_profiling_ = true;
      break;
    }
    case Microarchitecture::kIntelSkylake:
    case Microarchitecture::kIntelSkylakeServer:
    case Microarchitecture::kIntelCannonLake:
    case Microarchitecture::kIntelGoldmont:
    case Microarchitecture::kIntelGoldmontPlus:
    case Microarchitecture::kIntelTremont: {
      size_ = 32;
      callstack_profiling_ = true;
      break;
    }
    case Microarchitecture::kUnknown:
    case Microarchitecture::kAmdFamily0x15:
    case Microarchitecture::kAmdFamily0x16:
    case Microarchitecture::kAmdFamily0x17:
    case Microarchitecture::kAmdFamily0x19:
      break;
  }
  supported_ = supports_pdcm && (size_ > 0);
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
