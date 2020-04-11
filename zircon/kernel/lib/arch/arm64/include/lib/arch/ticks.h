// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_TICKS_H_
#define ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_TICKS_H_

#ifndef __ASSEMBLER__
#include <cstdint>

namespace arch {

// This is the C++ type that the assembly macro `sample_ticks` delivers.
// Higher-level kernel code knows how to translate this into the Zircon
// monotonic clock's zx_ticks_t.
struct EarlyTicks {
  uint64_t cntpct_el0, cntvct_el0;
};

}  // namespace arch

#else  // clang-format off

// Delivers an arch::EarlyTicks value in two registers \outp and \outv,
// as it's passed as a C++ function argument.  The remaining register
// arguments are needed for scratch space.
.macro sample_ticks outp, outv, tmp1, tmp2, tmp3
  mrs \outp, cntpct_el0
  mrs \outv, cntvct_el0

  // Workaround for Cortex-A73 erratum 858921.
  // See kernel/dev/timer/arm_generic/arm_generic_timer.cc::read_cntpct_a73.
  mrs \tmp1, cntpct_el0
  mrs \tmp2, cntvct_el0
  eor \tmp3, \outp, \tmp1
  tst \tmp3, #(1 << 32)
  csel \outp, \outp, \tmp1, eq
  eor \tmp3, \outv, \tmp2
  tst \tmp3, #(1 << 32)
  csel \outv, \outv, \tmp1, eq
.endm

#endif  // clang-format on

#endif  // ZIRCON_KERNEL_LIB_ARCH_ARM64_INCLUDE_LIB_ARCH_TICKS_H_
