// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_THREAD_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_THREAD_H_

#define CURRENT_PERCPU_PTR_OFFSET 16
#define CURRENT_SCSP_OFFSET 24

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/tls.h>

#include <arch/arm64/registers.h>
#include <arch/kernel_aspace.h>

// Bit 63 of the data_fault_resume field is used to indicate whether a data fault should first
// handle the fault, or immediately return the resume location. The 63'rd bit is selected as this
// bit is invariant over all kernel addresses.
static constexpr uint64_t ARM64_DFR_RUN_FAULT_HANDLER_BIT = 63;
// Check that the fault handler bit would always be 1 for a kernel address.
static_assert(((KERNEL_ASPACE_BASE >> ARM64_DFR_RUN_FAULT_HANDLER_BIT) & 1) == 1 &&
                  ((KERNEL_ASPACE_SIZE - 1) & KERNEL_ASPACE_BASE) == 0,
              "DFR fault handler bit not invariant over kernel addresses");

__BEGIN_CDECLS

struct fpstate {
  uint32_t fpcr;
  uint32_t fpsr;
  uint64_t regs[64];
};

struct arm64_percpu;

struct arch_thread {
  // The compiler (when it's Clang with -mtp=el1) knows the position of these
  // two fields relative to TPIDR_EL1, which is what
  // __builtin_thread_pointer() returns.  TPIDR_EL1 points just past these,
  // i.e. to &abi[1].
  uintptr_t stack_guard;
  vaddr_t unsafe_sp;
  union {
    char thread_pointer_location;
    vaddr_t sp;
  };

  // Debugger access to userspace general regs while suspended or stopped
  // in an exception.
  // The regs are saved on the stack and then a pointer is stored here.
  // Nullptr if not suspended or not stopped in an exception.
  // TODO(fxbug.dev/30521): Also nullptr for synthetic exceptions that don't provide
  // them yet.
  struct iframe_t* suspended_general_regs;

  // Point to the current cpu pointer when the thread is running, used to
  // restore the fixed register on exception entry. Swapped on context switch.
  struct arm64_percpu* current_percpu_ptr;

#if __has_feature(shadow_call_stack)
  uintptr_t* shadow_call_sp;
#endif

  // If non-NULL, address to return to on data fault. Additionally the
  // ARM64_DFR_RUN_FAULT_HANDLER_BIT controls whether the fault handler is invoked or not. If not
  // invoked resume is called with iframe_t::r[1] = fault address and iframe_t::r[2] = page fault
  // flags.
  uint64_t data_fault_resume;

  // User mode's TPIDR and TPIDRRO.
  //
  // TODO(maniscalco): User mode registers state is stored in two places, on the stack (|iframe_t|)
  // and here.  Consider consolidating and moving it all here so that it's in one place.  Motivation
  // is to reduce code complexity.
  uint64_t tpidr_el0;
  uint64_t tpidrro_el0;

  // Counts how many times the usermode thread generated the exception that
  // is used to restore |fpstate|. After some number of them it is more efficient to
  // restore fpu state eagerly and not pay for the exception itself.
  uint32_t fp_restore_count;
  // Saved fpu state.
  struct fpstate fpstate;

  // |track_debug_state| tells whether the kernel should keep track of the whole debug state for
  // this thread. Normally this is set explicitly by an user that wants to make use of HW
  // breakpoints or watchpoints.
  // Userspace can still read the complete |debug_state| even if |track_debug_state| is false.
  bool track_debug_state;
  arm64_debug_state_t debug_state;
};

#define thread_pointer_offsetof(field)        \
  ((int)offsetof(struct arch_thread, field) - \
   (int)offsetof(struct arch_thread, thread_pointer_location))

static_assert(thread_pointer_offsetof(stack_guard) == ZX_TLS_STACK_GUARD_OFFSET,
              "stack_guard field in wrong place");
static_assert(thread_pointer_offsetof(unsafe_sp) == ZX_TLS_UNSAFE_SP_OFFSET,
              "unsafe_sp field in wrong place");
static_assert(thread_pointer_offsetof(current_percpu_ptr) == CURRENT_PERCPU_PTR_OFFSET,
              "per cpu ptr offset in wrong place");
#if __has_feature(shadow_call_stack)
static_assert(thread_pointer_offsetof(shadow_call_sp) == CURRENT_SCSP_OFFSET,
              "shadow call stack pointer offset in wrong place");
#endif

__END_CDECLS

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARCH_THREAD_H_
