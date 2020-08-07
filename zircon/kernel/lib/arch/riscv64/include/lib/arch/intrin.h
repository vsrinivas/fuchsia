// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_INTRIN_H_
#define ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_INTRIN_H_

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

// Provide the machine-independent <lib/arch/intrin.h> API.

#ifdef __cplusplus

namespace arch {

/// Yield the processor momentarily.  This should be used in busy waits.
inline void Yield() { __asm__ volatile("nop"); }

/// Synchronize all memory accesses of all kinds.
inline void DeviceMemoryBarrier() { __asm__ volatile("fence iorw,iorw" ::: "memory");; /* __dsb(ARM_MB_SY); */ }

/// Synchronize the ordering of all memory accesses wrt other CPUs.
inline void ThreadMemoryBarrier() { __asm__ volatile("fence iorw,iorw" ::: "memory"); /* __dmb(ARM_MB_SY); */ }

/// Return the current CPU cycle count.
inline uint64_t Cycles() {
  uint64_t cycles;
  __asm__ volatile(
      "csrr   %0, cycle"
      : "=r" (cycles)
      :: "memory");
  return cycles;
}

}  // namespace arch

#endif  // __cplusplus

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_INTRIN_H_

