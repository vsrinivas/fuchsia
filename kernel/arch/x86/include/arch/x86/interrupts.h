// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

enum x86_interrupt_vector {
    X86_INT_DIVIDE_0 = 0,
    X86_INT_DEBUG,
    X86_INT_NMI,
    X86_INT_BREAKPOINT,
    X86_INT_OVERFLOW,
    X86_INT_BOUND_RANGE,
    X86_INT_INVALID_OP,
    X86_INT_DEVICE_NA,
    X86_INT_DOUBLE_FAULT,
    X86_INT_INVALID_TSS = 0xa,
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
    X86_INT_APIC_PMI,
    X86_INT_IPI_GENERIC,
    X86_INT_IPI_RESCHEDULE,
    X86_INT_IPI_HALT,

    X86_INT_MAX = 0xff,
    X86_INT_COUNT,
};
