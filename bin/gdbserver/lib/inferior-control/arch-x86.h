// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Parts of this file are derived from
// zircon/kernel/arch/x86/include/arch/x86/interrupts.h.

#pragma once

// x86 architectural values

namespace debugserver {
namespace arch {
namespace x86 {

constexpr unsigned EFLAGS_TF_MASK = 0x100;

enum InterruptVector {
  INT_DIVIDE_0 = 0,
  INT_DEBUG,
  INT_NMI,
  INT_BREAKPOINT,
  INT_OVERFLOW,
  INT_BOUND_RANGE,
  INT_INVALID_OP,
  INT_DEVICE_NA,
  INT_DOUBLE_FAULT,
  INT_COPROCESSOR_SEGMENT_OVERRUN, // i386 or earlier only
  INT_INVALID_TSS,
  INT_SEGMENT_NOT_PRESENT,
  INT_STACK_FAULT,
  INT_GP_FAULT,
  INT_PAGE_FAULT,
  INT_RESERVED,
  INT_FPU_FP_ERROR,
  INT_ALIGNMENT_CHECK,
  INT_MACHINE_CHECK,
  INT_SIMD_FP_ERROR,
  INT_VIRT,
  INT_MAX_INTEL_DEFINED = 0x1f,

  INT_PLATFORM_BASE = 0x20,
  INT_PLATFORM_MAX = 0xef,

  INT_LOCAL_APIC_BASE = 0xf0,
  INT_APIC_SPURIOUS = 0xf0,
  INT_APIC_TIMER,
  INT_APIC_ERROR,
  INT_IPI_GENERIC,
  INT_IPI_RESCHEDULE,
  INT_IPI_HALT,

  MAX_INT = 0xff,
};

}  // namespace x86
}  // namespace arch
}  // namespace debugserver
