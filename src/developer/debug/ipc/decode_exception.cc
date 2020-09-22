// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/decode_exception.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/debug/x86.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/shared/arch_arm64.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/developer/debug/shared/logging/logging.h"

namespace debug_ipc {
namespace {

// clang-format off
ExceptionType DecodeZircon(uint32_t code) {
  switch (code) {
    case ZX_EXCP_SW_BREAKPOINT: return ExceptionType::kSoftwareBreakpoint;
    case ZX_EXCP_HW_BREAKPOINT: return ExceptionType::kHardwareBreakpoint;
    case ZX_EXCP_GENERAL: return ExceptionType::kGeneral;
    case ZX_EXCP_FATAL_PAGE_FAULT: return ExceptionType::kPageFault;
    case ZX_EXCP_UNDEFINED_INSTRUCTION: return ExceptionType::kUndefinedInstruction;
    case ZX_EXCP_UNALIGNED_ACCESS: return ExceptionType::kUnalignedAccess;
    case ZX_EXCP_THREAD_STARTING: return ExceptionType::kThreadStarting;
    case ZX_EXCP_PROCESS_STARTING: return ExceptionType::kProcessStarting;
    case ZX_EXCP_THREAD_EXITING: return ExceptionType::kThreadExiting;
    case ZX_EXCP_POLICY_ERROR: return ExceptionType::kPolicyError;
    default:
      return ExceptionType::kUnknown;
  }
}
// clang-format on

}  // namespace

// x64 ---------------------------------------------------------------------------------------------

namespace {

ExceptionType DecodeHardwareRegister(uint64_t dr7, int slot) {
  // clang-format off
  bool is_watchpoint = false;
  switch (slot) {
    case 0: is_watchpoint = X86_DBG_CONTROL_RW0_GET(dr7) != 0; break;
    case 1: is_watchpoint = X86_DBG_CONTROL_RW1_GET(dr7) != 0; break;
    case 2: is_watchpoint = X86_DBG_CONTROL_RW2_GET(dr7) != 0; break;
    case 3: is_watchpoint = X86_DBG_CONTROL_RW3_GET(dr7) != 0; break;
    default:
      FX_NOTREACHED();
      return ExceptionType::kUnknown;
  }
  // clang-format on

  return is_watchpoint ? ExceptionType::kWatchpoint : ExceptionType::kHardwareBreakpoint;
}

}  // namespace

ExceptionType DecodeException(uint32_t code, const X64ExceptionInfo& info) {
  // All zircon exceptions don't need further analysis, except hardware which can represent a single
  // step, a hw breakpoint or a watchpoint.
  ExceptionType type = DecodeZircon(code);
  if (type != ExceptionType::kHardwareBreakpoint)
    return type;

  std::optional<X64ExceptionInfo::DebugRegs> regs;
  if (auto got = info.FetchDebugRegs()) {
    DEBUG_LOG(Archx64) << "DR6: " << debug_ipc::DR6ToString(got->dr6);
    regs = std::move(got.value());
  }

  // If we could not get the registers, we return the zircon exception. In the case of the ambiguous
  // hardware type, we assume single step.
  if (!regs.has_value())
    return ExceptionType::kSingleStep;

  // TODO(fxbug.dev/6246): This permits only one trigger per exception, when overlaps
  //                could occur. For a first pass this is acceptable.

  if (X86_DBG_STATUS_BS_GET(regs->dr6))
    return ExceptionType::kSingleStep;

  if (X86_DBG_STATUS_B0_GET(regs->dr6)) {
    return DecodeHardwareRegister(regs->dr7, 0);
  } else if (X86_DBG_STATUS_B1_GET(regs->dr6)) {
    return DecodeHardwareRegister(regs->dr7, 1);
  } else if (X86_DBG_STATUS_B2_GET(regs->dr6)) {
    return DecodeHardwareRegister(regs->dr7, 2);
  } else if (X86_DBG_STATUS_B3_GET(regs->dr6)) {
    return DecodeHardwareRegister(regs->dr7, 3);
  } else {
    FX_NOTREACHED() << "x86: No known hw exception set in DR6";
    return ExceptionType::kUnknown;
  }
}

// arm64 -------------------------------------------------------------------------------------------

namespace {

ExceptionType DecodeESR(uint32_t esr) {
  // The ESR register holds information about the last exception in the form of:
  // |31      26|25|24                              0|
  // |    EC    |IL|             ISS                 |
  //
  // Where:
  // - EC: Exception class field (what exception occurred).
  // - IL: Instruction length (whether the trap was 16-bit of 32-bit instruction).
  // - ISS: Instruction Specific Syndrome. The value is specific to each EC.
  uint32_t ec = esr >> 26;

  switch (ec) {
    case 0b111000: /* BRK from arm32 */
    case 0b111100: /* BRK from arm64 */
      return ExceptionType::kSoftwareBreakpoint;
    case 0b110000: /* HW breakpoint from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      return ExceptionType::kHardwareBreakpoint;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      return ExceptionType::kSingleStep;
    case 0b110100: /* HW watchpoint from a lower level */
    case 0b110101: /* HW watchpoint from same level */
      return ExceptionType::kWatchpoint;
    default:
      break;
  }

  return ExceptionType::kUnknown;
}

}  // namespace

ExceptionType DecodeException(uint32_t code, const Arm64ExceptionInfo& info) {
  // HW exceptions have to be analysed further.
  ExceptionType type = DecodeZircon(code);
  if (type != ExceptionType::kHardwareBreakpoint)
    return type;

  uint32_t esr;

  if (auto got = info.FetchESR()) {
    esr = *got;
  } else {
    return ExceptionType::kUnknown;
  }

  auto decoded_type = DecodeESR(esr);
  if (decoded_type == ExceptionType::kUnknown) {
    FX_NOTREACHED() << "Received invalid ESR value: 0x" << std::hex << esr << " (EC: 0x"
                    << (esr >> 26) << ").";
    return debug_ipc::ExceptionType::kUnknown;
  }

  return decoded_type;
}

}  // namespace debug_ipc
