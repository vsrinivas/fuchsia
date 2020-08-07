// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_TICKS_H_
#define ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_TICKS_H_

#ifndef __ASSEMBLER__
#include <cstdint>

namespace arch {

// This is the C++ type that the assembly macro `sample_ticks` delivers.
// Higher-level kernel code knows how to translate this into the Zircon
// monotonic clock's zx_ticks_t.
struct EarlyTicks {
  uint64_t placeholder;

  [[gnu::always_inline]] static EarlyTicks Get() {
    return {0};
  }
};

}  // namespace arch

#else  // clang-format off

// Delivers an arch::EarlyTicks value in two registers \outp and \outv,
// as it's passed as a C++ function argument.  The remaining register
// arguments are needed for scratch space.
.macro sample_ticks outp, outv, tmp1, tmp2, tmp3
.endm

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_TICKS_H_

