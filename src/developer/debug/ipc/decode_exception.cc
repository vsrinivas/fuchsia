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

NotifyException::Type DecodeZircon(uint32_t code) {
  if (code == ZX_EXCP_SW_BREAKPOINT) {
    return NotifyException::Type::kSoftware;
  } else if (code == ZX_EXCP_HW_BREAKPOINT) {
    return NotifyException::Type::kLast;
  } else {
    return NotifyException::Type::kGeneral;
  }
}

NotifyException::Type DecodeESR(uint32_t esr) {
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
      return NotifyException::Type::kSoftware;
    case 0b110000: /* HW breakpoint from a lower level */
    case 0b110001: /* HW breakpoint from same level */
      return NotifyException::Type::kHardware;
    case 0b110010: /* software step from lower level */
    case 0b110011: /* software step from same level */
      return NotifyException::Type::kSingleStep;
    default:
      break;
  }

  return NotifyException::Type::kGeneral;
}

}  // namespace

NotifyException::Type DecodeException(uint32_t code, X64ExceptionInfo* info) {
  auto ret = DecodeZircon(code);

  if (ret != NotifyException::Type::kLast) {
    return ret;
  }

  X64ExceptionInfo::DebugRegs regs;

  if (auto got = info->FetchDebugRegs()) {
    regs = *got;
  } else {
    return NotifyException::Type::kSingleStep;
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
    return NotifyException::Type::kSingleStep;
  } else {
    FXL_NOTREACHED() << "x86: No known hw exception set in DR6";
  }

  if (info->AddrIsWatchpoint(exception_address)) {
    return NotifyException::Type::kWatchpoint;
  }

  return NotifyException::Type::kHardware;
}

NotifyException::Type DecodeException(uint32_t code, Arm64ExceptionInfo* info) {
  auto ret = DecodeZircon(code);

  if (ret != NotifyException::Type::kLast) {
    return ret;
  }

  uint32_t esr;

  if (auto got = info->FetchESR()) {
    esr = *got;
  } else {
    return NotifyException::Type::kGeneral;
  }

  auto decoded_type = DecodeESR(esr);

  if (decoded_type == debug_ipc::NotifyException::Type::kSingleStep ||
      decoded_type == debug_ipc::NotifyException::Type::kHardware) {
    return decoded_type;
  }

  FXL_NOTREACHED() << "Received invalid ESR value: 0x" << std::hex << esr;
  return debug_ipc::NotifyException::Type::kGeneral;
}

}  // namespace debug_ipc
