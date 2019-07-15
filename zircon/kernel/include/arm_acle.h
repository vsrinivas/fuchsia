// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARM_ACLE_H_
#define ZIRCON_KERNEL_INCLUDE_ARM_ACLE_H_

// ARM C Language Extensions header for Zircon.
// This header includes the toolchain-provided ACLE implementation and fills in the missing pieces.

// Include arm_acle.h from the toolchain headers.
#include_next <arm_acle.h>
#include <stdint.h>

#ifndef __clang__

// GCC's arm_acle.h is missing implementations of the following ARM-standard APIs.

// From ARM ACLE spec, 8.3 Memory Barriers
//   "Memory barriers ensure specific ordering properties between memory accesses. ... The
// intrinsics in this section are available for all targets. They may be no-ops (i.e. generate no
// code, but possibly act as a code motion barrier in compilers) on targets where the relevant
// instructions do not exist, but only if the property they guarantee would have held anyway. On
// targets where the relevant instructions exist but are implemented as no-ops, these intrinsics
// generate the instructions."

// The |mb| parameter to __dmb(), __dsb(), and __isb() determines the domain and direction
// of the barrier. It is an integer documented in The ACLE spec section 8.3. Zircon provides
// defined constants for these values in arm64.h.

// These instructions contain the "memory" clobber option although they do not specifically modify
// the contents of memory. This option acts as an atomic_signal_fence() (compiler barrier).

// Data Memory Barrier.
#define __dmb(mb) __asm__ volatile("dmb %0" ::"i"(mb) : "memory")
// Data Synchronization Barrier.
#define __dsb(mb) __asm__ volatile("dsb %0" ::"i"(mb) : "memory")
// Instruction Synchronization Barrier.
#define __isb(mb) __asm__ volatile("isb %0" ::"i"(mb) : "memory")

// From ARM ACLE spec, 8.4 Hints
//   "The intrinsics in this section are available for all targets. They may be no-ops (i.e.
// generate no code, but possibly act as a code motion barrier in compilers) on targets where the
// relevant instructions do not exist. On targets where the relevant instructions exist but are
// implemented as no-ops, these intrinsics generate the instructions.

// These instructions contain the "memory" clobber option although they do not affect memory.
// This option acts as an atomic_signal_fence() (compiler barrier).

// Set Event.
#define __sev() __asm__ volatile("sev" ::: "memory")
// Set Event Local.
#define __sevl() __asm__ volatile("sevl" ::: "memory")
// Wait For Event.
#define __wfe() __asm__ volatile("wfe" ::: "memory")
// Wait For Interrupt.
#define __wfi() __asm__ volatile("wfi" ::: "memory")
// Yield.
#define __yield() __asm__ volatile("yield" ::: "memory")

// Read (MRS) or write (MSR) a system register.
// Registers may be referenced with a symbolic name string, such as "tpidrro_el0" or by the
// op string in the form "o0:op1:CRn:CRm:op2", where
//   <o0> is a decimal integer in the range [0, 1]
//   <op1>, <op2> are decimal integers in the range [0, 7]
//   <CRm>, <CRn> are decimal integers in the range [0, 15]

// An ISB op is required to guarantee a register write has completed. The effects of the
// write may not be visible until the ISB has been issued.
// Call __isb() after one or more __arm_wsr() calls.

// Read 64-bit system register.
#define __arm_rsr64(reg)                          \
  ({                                              \
    uint64_t _val;                                \
    __asm__ volatile("mrs %0," reg : "=r"(_val)); \
    _val;                                         \
  })

// Read 32-bit system register.
#define __arm_rsr(reg)                            \
  ({                                              \
    uint32_t _val;                                \
    __asm__ volatile("mrs %0," reg : "=r"(_val)); \
    _val;                                         \
  })

// Write 64-bit system register.
#define __arm_wsr64(reg, val)                        \
  ({                                                 \
    uint64_t _val = (val);                           \
    __asm__ volatile("msr " reg ", %0" ::"r"(_val)); \
  })

// Write 32-bit system register.
#define __arm_wsr(reg, val)                          \
  ({                                                 \
    uint32_t _val = (val);                           \
    __asm__ volatile("msr " reg ", %0" ::"r"(_val)); \
  })

#endif  // !__clang__

#endif  // ZIRCON_KERNEL_INCLUDE_ARM_ACLE_H_
