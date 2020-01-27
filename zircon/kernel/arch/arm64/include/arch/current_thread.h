// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_

/* use the cpu local thread context pointer to store current_thread */
static inline thread_t* get_current_thread(void) {
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
  tp -= offsetof(thread_t, arch.thread_pointer_location);
  return (thread_t*)tp;
}

static inline void set_current_thread(thread_t* t) {
  __arm_wsr64("tpidr_el1", (uint64_t)&t->arch.thread_pointer_location);
  __isb(ARM_MB_SY);
}

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_CURRENT_THREAD_H_
