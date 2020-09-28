// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_INTRIN_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_INTRIN_H_

// Constants from ACLE section 8.3, used as the argument for __dmb(),
// __dsb(), and __isb().  Values are the architecturally defined
// immediate values encoded in barrier instructions DMB, DSB, and ISB.

#define ARM_MB_OSHLD 0x1
#define ARM_MB_OSHST 0x2
#define ARM_MB_OSH 0x3

#define ARM_MB_NSHLD 0x5
#define ARM_MB_NSHST 0x6
#define ARM_MB_NSH 0x7

#define ARM_MB_ISHLD 0x9
#define ARM_MB_ISHST 0xa
#define ARM_MB_ISH 0xb

#define ARM_MB_LD 0xd
#define ARM_MB_ST 0xe
#define ARM_MB_SY 0xf

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

// TODO(mcgrathr): Until https://gcc.gnu.org/bugzilla/show_bug.cgi?id=94993 is
// fixed, <arm_acle.h> won't compile properly under -mgeneral-regs-only.  We
// need that switch on for the whole kernel, but to work around this bug we
// need it temporarily off while processing <arm_acle.h>.  The push_options and
// pop_options pragmas provide a way to do things like this.  However, there is
// no -mno-general-regs-only, just -mgeneral-regs-only, so it's not possible to
// use `#pragma GCC target ("no-general-regs-only")` here, as would be more
// straightforward (relatively speaking).  Instead, we use `-include` to feed
// kernel/arch/arm64/general-regs-only.h into every translation unit, and that
// does `push_options` and `target ("general-regs-only")`.  So now we can use
// `pop_options` here to unset -mgeneral-regs-only, and then restore it
// afterwards by repeating the same `push_options` and `target` pragmas below.
#if __GNUC__ == 10
#pragma GCC pop_options
#endif  // __GNUC__ == 10

// Provide the standard ARM C Language Extensions API.
#include <arm_acle.h>

#if __GNUC__ == 10
#pragma GCC push_options
#pragma GCC target ("general-regs-only")
#endif  // __GNUC__ == 10

#ifndef __clang__
// GCC's <arm_acle.h> is missing implementations of the following APIs
// specified by ARM, so they are filled in here.  Clang's implementation
// is already complete.

// From ARM ACLE spec, 8.3 Memory Barriers:
//   "Memory barriers ensure specific ordering properties between memory
// accesses. ... The intrinsics in this section are available for all
// targets. They may be no-ops (i.e. generate no code, but possibly act
// as a code motion barrier in compilers) on targets where the relevant
// instructions do not exist, but only if the property they guarantee
// would have held anyway. On targets where the relevant instructions
// exist but are implemented as no-ops, these intrinsics generate the
// instructions."

// The |mb| parameter to __dmb(), __dsb(), and __isb() determines the
// domain and direction of the barrier. It is an integer documented in
// The ACLE spec section 8.3. Zircon provides defined constants for
// these values in arm64.h.

// These instructions contain the "memory" clobber option although they
// do not specifically modify the contents of memory. This option acts
// as an atomic_signal_fence() (compiler barrier).

// Data Memory Barrier.
#define __dmb(mb) __asm__ volatile("dmb %0" ::"i"(mb) : "memory")
// Data Synchronization Barrier.
#define __dsb(mb) __asm__ volatile("dsb %0" ::"i"(mb) : "memory")
// Instruction Synchronization Barrier.
#define __isb(mb) __asm__ volatile("isb %0" ::"i"(mb) : "memory")

// From ARM ACLE spec, 8.4 Hints:
//   "The intrinsics in this section are available for all targets. They
// may be no-ops (i.e.  generate no code, but possibly act as a code
// motion barrier in compilers) on targets where the relevant
// instructions do not exist. On targets where the relevant instructions
// exist but are implemented as no-ops, these intrinsics generate the
// instructions.

// These instructions contain the "memory" clobber option although they
// do not affect memory.  This option acts as an atomic_signal_fence()
// (compiler barrier).

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
// Registers may be referenced with a symbolic name string, such as
// "tpidrro_el0" or by the op string in the form "o0:op1:CRn:CRm:op2", where:
//   <o0> is a decimal integer in the range [0, 1]
//   <op1>, <op2> are decimal integers in the range [0, 7]
//   <CRm>, <CRn> are decimal integers in the range [0, 15]
// An ISB op is required to guarantee a register write has
// completed. The effects of the write may not be visible until the ISB
// has been issued.  Call __isb() after one or more __arm_wsr() calls.

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

// Provide the machine-independent <lib/arch/intrin.h> API.

#ifdef __cplusplus

namespace arch {

/// Yield the processor momentarily.  This should be used in busy waits.
inline void Yield() { __yield(); }

// TODO(fxbug.dev/49941): Improve the docs on the barrier APIs, maybe rename/refine.

/// Synchronize all memory accesses of all kinds.
inline void DeviceMemoryBarrier() { __dsb(ARM_MB_SY); }

/// Synchronize the ordering of all memory accesses wrt other CPUs.
inline void ThreadMemoryBarrier() { __dmb(ARM_MB_SY); }

/// Return the current CPU cycle count.
inline uint64_t Cycles() { return __arm_rsr64("pmccntr_el0"); }

}  // namespace arch

#endif  // __cplusplus

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_INTRIN_H_
