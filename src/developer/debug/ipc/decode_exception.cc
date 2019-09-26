// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/decode_exception.h"

#include <zircon/syscalls/exception.h>

#include "src/developer/debug/shared/arch_arm64.h"
#include "src/developer/debug/shared/arch_x86.h"
#include "src/lib/fxl/logging.h"

namespace debug_ipc {
namespace {

ExceptionType DecodeZircon(uint32_t code) {
  if (code == ZX_EXCP_SW_BREAKPOINT) {
    return ExceptionType::kSoftware;
  } else if (code == ZX_EXCP_HW_BREAKPOINT) {
    return ExceptionType::kLast;
  } else if (code == ZX_EXCP_GENERAL) {
    return ExceptionType::kGeneral;
  } else if (code == ZX_EXCP_FATAL_PAGE_FAULT) {
    return ExceptionType::kPageFault;
  } else if (code == ZX_EXCP_UNDEFINED_INSTRUCTION) {
    return ExceptionType::kUndefinedInstruction;
  } else if (code == ZX_EXCP_UNALIGNED_ACCESS) {
    return ExceptionType::kUnalignedAccess;
  } else if (code == ZX_EXCP_THREAD_STARTING) {
    return ExceptionType::kThreadStarting;
  } else if (code == ZX_EXCP_PROCESS_STARTING) {
    return ExceptionType::kProcessStarting;
  } else if (code == ZX_EXCP_THREAD_EXITING) {
    return ExceptionType::kThreadExiting;
  } else if (code == ZX_EXCP_POLICY_ERROR) {
    return ExceptionType::kPolicyError;
  } else {
    return ExceptionType::kUnknown;
  }
}

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
      return ExceptionType::kSoftware;
    case 0b110000: /* HW breakpoint from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      return ExceptionType::kHardware;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      return ExceptionType::kSingleStep;
    default:
      break;
  }

  return ExceptionType::kUnknown;
}

}  // namespace

ExceptionType DecodeException(uint32_t code, X64ExceptionInfo* info) {
  auto ret = DecodeZircon(code);

  if (ret != ExceptionType::kLast) {
    return ret;
  }

  X64ExceptionInfo::DebugRegs regs;

  if (auto got = info->FetchDebugRegs()) {
    regs = *got;
  } else {
    return ExceptionType::kSingleStep;
  }

  // TODO(DX-1445): This permits only one trigger per exception, when overlaps
  //                could occur. For a first pass this is acceptable.
  uint64_t exception_address = 0;
  // HW breakpoints have priority over single-step.
  if (X86_FLAG_VALUE(regs.dr6, DR6B0)) {
    exception_address = regs.dr0;
  } else if (X86_FLAG_VALUE(regs.dr6, DR6B1)) {
    exception_address = regs.dr1;
  } else if (X86_FLAG_VALUE(regs.dr6, DR6B2)) {
    exception_address = regs.dr2;
  } else if (X86_FLAG_VALUE(regs.dr6, DR6B3)) {
    exception_address = regs.dr3;
  } else if (X86_FLAG_VALUE(regs.dr6, DR6BS)) {
    return ExceptionType::kSingleStep;
  } else {
    FXL_NOTREACHED() << "x86: No known hw exception set in DR6";
  }

  if (info->AddrIsWatchpoint(exception_address)) {
    return ExceptionType::kWatchpoint;
  }

  return ExceptionType::kHardware;
}

ExceptionType DecodeException(uint32_t code, Arm64ExceptionInfo* info) {
  auto ret = DecodeZircon(code);

  if (ret != ExceptionType::kLast) {
    return ret;
  }

  uint32_t esr;

  if (auto got = info->FetchESR()) {
    esr = *got;
  } else {
    return ExceptionType::kUnknown;
  }

  auto decoded_type = DecodeESR(esr);

  if (decoded_type == debug_ipc::ExceptionType::kSingleStep ||
      decoded_type == debug_ipc::ExceptionType::kHardware) {
    return decoded_type;
  }

  FXL_NOTREACHED() << "Received invalid ESR value: 0x" << std::hex << esr;
  return debug_ipc::ExceptionType::kUnknown;
}

}  // namespace debug_ipc
