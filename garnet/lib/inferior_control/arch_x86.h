// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Parts of this file are derived from
// zircon/kernel/arch/x86/include/arch/x86/interrupts.h.

#pragma once

// x86 architectural values

namespace inferior_control {

constexpr unsigned X86_EFLAGS_TF_MASK = 0x100;

enum X86InterruptVector {
  X86_INT_DIVIDE_0 = 0,
  X86_INT_DEBUG,
  X86_INT_NMI,
  X86_INT_BREAKPOINT,
  X86_INT_OVERFLOW,
  X86_INT_BOUND_RANGE,
  X86_INT_INVALID_OP,
  X86_INT_DEVICE_NA,
  X86_INT_DOUBLE_FAULT,
  X86_INT_COPROCESSOR_SEGMENT_OVERRUN,  // i386 or earlier only
  X86_INT_INVALID_TSS,
  X86_INT_SEGMENT_NOT_PRESENT,
  X86_INT_STACK_FAULT,
  X86_INT_GP_FAULT,
  X86_INT_PAGE_FAULT,
  X86_INT_RESERVED,
  X86_INT_FPU_FP_ERROR,
  X86_INT_ALIGNMENT_CHECK,
  X86_INT_MACHINE_CHECK,
  X86_INT_SIMD_FP_ERROR,
  X86_INT_VIRT,
  X86_INT_MAX_INTEL_DEFINED = 0x1f,

  X86_INT_PLATFORM_BASE = 0x20,
  X86_INT_PLATFORM_MAX = 0xef,

  X86_INT_LOCAL_APIC_BASE = 0xf0,
  X86_INT_APIC_SPURIOUS = 0xf0,
  X86_INT_APIC_TIMER,
  X86_INT_APIC_ERROR,
  X86_INT_IPI_GENERIC,
  X86_INT_IPI_RESCHEDULE,
  X86_INT_IPI_HALT,

  MAX_X86_INT = 0xff,
};

}  // namespace inferior_control
