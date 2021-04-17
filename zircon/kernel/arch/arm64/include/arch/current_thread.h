// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_

#include <lib/arch/intrin.h>

// Routines to directly access the current thread pointer out of the current
// cpu structure pointed the TPIDR_EL1 register.

// NOTE: must be included after the definition of Thread due to the offsetof
// applied in the following routines.

// Use the cpu local thread context pointer to store current_thread.
static inline Thread* arch_get_current_thread() {
#ifdef __clang__
  // Clang with --target=aarch64-fuchsia -mtp=el1 reads
  // TPIDR_EL1 for __builtin_thread_pointer (instead of the usual
  // TPIDR_EL0 for user mode).  Using the intrinsic instead of asm
  // lets the compiler understand what it's doing a little better,
  // which conceivably could let it optimize better.
  char* tp = (char*)__builtin_thread_pointer();
#else
  char* tp = (char*)__arm_rsr64("tpidr_el1");
#endif

  // The Thread structure isn't standard layout, but it's "POD enough"
  // for us to rely on computing this member offset via offsetof.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
  tp -= offsetof(Thread, arch_.thread_pointer_location);
#pragma GCC diagnostic pop

  return (Thread*)tp;
}

static inline void arch_set_current_thread(Thread* t) {
  __arm_wsr64("tpidr_el1", (uint64_t)&t->arch().thread_pointer_location);
}

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_
