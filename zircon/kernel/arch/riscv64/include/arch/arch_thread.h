// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_THREAD_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_THREAD_H_

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/tls.h>

#include <arch/kernel_aspace.h>

__BEGIN_CDECLS

struct riscv64_percpu;

struct arch_thread {
  vaddr_t sp;

  // Point to the current cpu pointer when the thread is running, used to
  // restore the fixed register on exception entry. Swapped on context switch.
  struct riscv64_percpu* current_percpu_ptr;

  // If non-NULL, address to return to on data fault.
  uint64_t data_fault_resume;
};

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_ARCH_THREAD_H_
