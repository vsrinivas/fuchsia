// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_TICKS_H_
#define ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_TICKS_H_

#include <lib/arch/intrin.h>

#ifndef __ASSEMBLER__
#include <cstdint>

namespace arch {

// This is the C++ type that the assembly macro `sample_ticks` delivers.
// Higher-level kernel code knows how to translate this into the Zircon
// monotonic clock's zx_ticks_t.
struct EarlyTicks {
  uint64_t tsc;

  static EarlyTicks Get() { return {_rdtsc()}; }
};

}  // namespace arch

#else  // clang-format off

// Delivers an arch::EarlyTicks value in %rax.  Clobbers %rdx.
// In 32-bit mode, delivers the value in %eax, %edx.
.macro sample_ticks
  rdtsc
#ifdef __x86_64__
  lsh $32, %rdx
  or %rdx, %rax
#endif
.endm

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_X86_INCLUDE_LIB_ARCH_TICKS_H_
