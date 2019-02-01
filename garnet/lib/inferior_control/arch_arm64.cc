// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/fxl/logging.h"

namespace inferior_control {

GdbSignal ComputeGdbSignal(const zx_exception_context_t& context) {
  // Initialize just in case something goes wrong below.
  GdbSignal sigval = GdbSignal::kSegv;
  uint32_t esr = context.arch.u.arm_64.esr;
  uint32_t exception_class = esr >> 26;

  // Note: While the arm32 stuff can't currently happen, we leave them in
  // for documentation purposes.

  switch (exception_class) {
    case 0b000000: /* unknown reason */
      sigval = GdbSignal::kSegv;
      break;
    case 0b111000: /* BRK from arm32 */
    case 0b111100: /* BRK from arm64 */
      sigval = GdbSignal::kTrap;
      break;
    case 0b000111: /* floating point */
      sigval = GdbSignal::kFpe;
      break;
    case 0b010001: /* syscall from arm32 */
    case 0b010101: /* syscall from arm64 */
      FXL_NOTREACHED();
      break;
    case 0b100000: /* instruction abort from lower level */
    case 0b100001: /* instruction abort from same level */
      sigval = GdbSignal::kIll;
      break;
    case 0b100100: /* data abort from lower level */
    case 0b100101: /* data abort from same level */
      sigval = GdbSignal::kSegv;
      break;
    default:
      // TODO(dje): grok more values
      sigval = GdbSignal::kSegv;
      break;
  }

  FXL_VLOG(1) << "ARM64 exception class (" << exception_class
              << ") mapped to: " << static_cast<int>(sigval);

  return sigval;
}

bool IsSingleStepException(const zx_exception_context_t& context) {
  FXL_NOTIMPLEMENTED();
  return false;
}

void DumpArch(FILE* out) { FXL_NOTIMPLEMENTED(); }

}  // namespace inferior_control
