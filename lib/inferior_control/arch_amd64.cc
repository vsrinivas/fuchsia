// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arch.h"

#include "lib/fxl/logging.h"

#include "garnet/lib/debugger_utils/util.h"
#include "garnet/lib/debugger_utils/x86_cpuid.h"
#include "garnet/lib/debugger_utils/x86_pt.h"

#include "arch_x86.h"
#include "thread.h"

namespace debugserver {

GdbSignal ComputeGdbSignal(const zx_exception_context_t& context) {
  GdbSignal sigval;
  auto arch_exception = context.arch.u.x86_64.vector;

  switch (arch_exception) {
    case X86_INT_DIVIDE_0:
      sigval = GdbSignal::kFpe;
      break;
    case X86_INT_DEBUG:
      sigval = GdbSignal::kTrap;
      break;
    case X86_INT_NMI:
      sigval = GdbSignal::kUrg;
      break;
    case X86_INT_BREAKPOINT:
      sigval = GdbSignal::kTrap;
      break;
    case X86_INT_OVERFLOW:
      sigval = GdbSignal::kFpe;
      break;
    case X86_INT_BOUND_RANGE:
      sigval = GdbSignal::kSegv;
      break;
    case X86_INT_INVALID_OP:
      sigval = GdbSignal::kIll;
      break;
    case X86_INT_DEVICE_NA:  // e.g., Coprocessor Not Available
      sigval = GdbSignal::kFpe;
      break;
    case X86_INT_DOUBLE_FAULT:
      sigval = GdbSignal::kEmt;
      break;
    case X86_INT_COPROCESSOR_SEGMENT_OVERRUN:
    case X86_INT_INVALID_TSS:
    case X86_INT_SEGMENT_NOT_PRESENT:
    case X86_INT_STACK_FAULT:
    case X86_INT_GP_FAULT:
    case X86_INT_PAGE_FAULT:
      sigval = GdbSignal::kSegv;
      break;
    case X86_INT_RESERVED:
      sigval = GdbSignal::kUsr1;
      break;
    case X86_INT_FPU_FP_ERROR:
    case X86_INT_ALIGNMENT_CHECK:
      sigval = GdbSignal::kEmt;
      break;
    case X86_INT_MACHINE_CHECK:
      sigval = GdbSignal::kUrg;
      break;
    case X86_INT_SIMD_FP_ERROR:
      sigval = GdbSignal::kFpe;
      break;
    case X86_INT_VIRT:  // Virtualization Exception
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

  return arch_exception == X86_INT_DEBUG;
}

void DumpArch(FILE* out) { x86_feature_debug(out); }

}  // namespace debugserver
