// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include "private.h"

#ifdef __x86_64__
#include <x86intrin.h>
#endif

zx_ticks_t _zx_ticks_get(void) {
#if __aarch64__
  // read the virtual counter
  zx_ticks_t ticks;
  __asm__ volatile("mrs %0, cntvct_el0" : "=r"(ticks));
  return ticks;
#elif __x86_64__
  return __rdtsc();
#else
#error Unsupported architecture
#endif
}

VDSO_INTERFACE_FUNCTION(zx_ticks_get);

// Note: See alternates.ld for a definition of CODE_ticks_get_via_kernel, which
// is an alias for SYSCALL_zx_ticks_get_via_kernel.  This is a version of
// zx_ticks_get which goes through a forced syscall.  It is selected by the vDSO
// builder at runtiem for use on platforms where the hardware tick counter is
// not directly accessible by user mode code.
