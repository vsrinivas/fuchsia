// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/mp.h>

#ifndef ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_CURRENT_THREAD_H_
#define ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_CURRENT_THREAD_H_

static inline Thread* arch_get_current_thread(void) {
  /* Read directly from gs, rather than via x86_get_percpu()->current_thread,
   * so that this is atomic.  Otherwise, we could context switch between the
   * read of percpu from gs and the read of the current_thread pointer, and
   * discover the current thread on a different CPU */
  return (Thread*)x86_read_gs_offset64(PERCPU_CURRENT_THREAD_OFFSET);
}

static inline void arch_set_current_thread(Thread* t) {
  /* See above for why this is a direct gs write */
  x86_write_gs_offset64(PERCPU_CURRENT_THREAD_OFFSET, (uint64_t)t);
}

#endif  // ZIRCON_KERNEL_ARCH_X86_INCLUDE_ARCH_CURRENT_THREAD_H_
