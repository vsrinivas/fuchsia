// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

#ifdef __x86_64__
#include <x86intrin.h>
#endif

#include <stdatomic.h>

namespace {

#if __aarch64__
inline zx_ticks_t get_raw_ticks() {
  // read the virtual counter
  zx_ticks_t ticks;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
  return ticks;
}

inline zx_ticks_t get_raw_ticks_arm_a73() {
  zx_ticks_t ticks1, ticks2;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks1));
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks2));
  return (((ticks1 ^ ticks2) >> 32) & 1) ? ticks1 : ticks2;
}
#elif __x86_64__
inline zx_ticks_t get_raw_ticks() { return __rdtsc(); }
#else
#error Unsupported architecture
#endif

}  // namespace

// TODO(fxb/91701): switch to the ABA method of reading the offset when we start
// to allow the offset to be changed as a result of coming out of system
// suspend.
__EXPORT zx_ticks_t _zx_ticks_get(void) {
  return get_raw_ticks() + DATA_CONSTANTS.raw_ticks_to_ticks_offset;
}

#if __aarch64__
// This is a specialized version of zx_ticks_get used to work around Cortex-A73
// erratum 858921 described in:
//
// https://static.docs.arm.com/epm086451/120/Cortex-A73_MPCore_Software_Developers_Errata_Notice.pdf
//
// This 2x read technique is the same technique currently being used in the
// kernel to mitigate the errata.  It will be selected by the kernel during VDSO
// construction if needed.
VDSO_KERNEL_EXPORT zx_ticks_t CODE_ticks_get_arm_a73(void) {
  return get_raw_ticks_arm_a73() + DATA_CONSTANTS.raw_ticks_to_ticks_offset;
}
#endif

VDSO_INTERFACE_FUNCTION(zx_ticks_get);

// Note: See alternates.ld for a definition of CODE_ticks_get_via_kernel, which
// is an alias for SYSCALL_zx_ticks_get_via_kernel.  This is a version of
// zx_ticks_get which goes through a forced syscall.  It is selected by the vDSO
// builder at runtime for use on platforms where the hardware tick counter is
// not directly accessible by user mode code.
