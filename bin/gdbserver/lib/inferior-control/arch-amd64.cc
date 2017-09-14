// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/fxl/logging.h"

#include "debugger-utils/util.h"
#include "debugger-utils/x86-cpuid.h"
#include "debugger-utils/x86-pt.h"

#include "arch-x86.h"
#include "thread.h"

namespace debugserver {
namespace arch {

GdbSignal ComputeGdbSignal(const zx_exception_context_t& context) {
  GdbSignal sigval;
  auto arch_exception = context.arch.u.x86_64.vector;

  switch (arch_exception) {
    case x86::INT_DIVIDE_0:
      sigval = GdbSignal::kFpe;
      break;
    case x86::INT_DEBUG:
      sigval = GdbSignal::kTrap;
      break;
    case x86::INT_NMI:
      sigval = GdbSignal::kUrg;
      break;
    case x86::INT_BREAKPOINT:
      sigval = GdbSignal::kTrap;
      break;
    case x86::INT_OVERFLOW:
      sigval = GdbSignal::kFpe;
      break;
    case x86::INT_BOUND_RANGE:
      sigval = GdbSignal::kSegv;
      break;
    case x86::INT_INVALID_OP:
      sigval = GdbSignal::kIll;
      break;
    case x86::INT_DEVICE_NA:  // e.g., Coprocessor Not Available
      sigval = GdbSignal::kFpe;
      break;
    case x86::INT_DOUBLE_FAULT:
      sigval = GdbSignal::kEmt;
      break;
    case x86::INT_COPROCESSOR_SEGMENT_OVERRUN:
    case x86::INT_INVALID_TSS:
    case x86::INT_SEGMENT_NOT_PRESENT:
    case x86::INT_STACK_FAULT:
    case x86::INT_GP_FAULT:
    case x86::INT_PAGE_FAULT:
      sigval = GdbSignal::kSegv;
      break;
    case x86::INT_RESERVED:
      sigval = GdbSignal::kUsr1;
      break;
    case x86::INT_FPU_FP_ERROR:
    case x86::INT_ALIGNMENT_CHECK:
      sigval = GdbSignal::kEmt;
      break;
    case x86::INT_MACHINE_CHECK:
      sigval = GdbSignal::kUrg;
      break;
    case x86::INT_SIMD_FP_ERROR:
      sigval = GdbSignal::kFpe;
      break;
    case x86::INT_VIRT:  // Virtualization Exception
      sigval = GdbSignal::kVtalrm;
      break;
    case 21:  // Control Protection Exception
      sigval = GdbSignal::kSegv;
      break;
    case 22 ... 31:
      sigval = GdbSignal::kUsr1;  // reserved (-> SIGUSR1 for now)
      break;
    default:
      sigval = GdbSignal::kUsr2;  // "software generated" (-> SIGUSR2 for now)
      break;
  }

  FXL_VLOG(1) << "x86 (AMD64) exception (" << arch_exception
              << ") mapped to: " << static_cast<int>(sigval);

  return sigval;
}

bool IsSingleStepException(const zx_exception_context_t& context) {
  auto arch_exception = context.arch.u.x86_64.vector;

  return arch_exception == x86::INT_DEBUG;
}

void DumpArch(FILE* out) {
  x86::x86_feature_debug(out);
}

}  // namespace arch
}  // namespace debugserver
