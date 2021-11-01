// Copyright 2021 The Fuchsia Authors>
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_EXCEPTION_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_EXCEPTION_H_

// PhysException usually does not return at all.  If it does return, it must
// return this exact value.  Then the assembly code will return to the
// interrupted register state (which may have been modified by the handler).
#define PHYS_EXCEPTION_RESUME 0xb002dead1badd00d

#ifndef __ASSEMBLER__
#include <stdint.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include "main.h"
#include "stack.h"

struct alignas(BOOT_STACK_ALIGN) PhysExceptionState {
#if defined(__aarch64__)

  uint64_t pc() const { return regs.pc; }
  uint64_t sp() const { return regs.sp; }
  uint64_t psr() const { return regs.cpsr; }
  uint64_t fp() const { return regs.r[29]; }
  uint64_t shadow_call_sp() const {
#if __has_feature(shadow_call_stack)
    return regs.r[18];
#endif
    return 0;
  }

#elif defined(__x86_64__)

  uint64_t pc() const { return regs.rip; }
  uint64_t sp() const { return regs.rip; }
  uint64_t psr() const { return regs.rflags; }
  uint64_t fp() const { return regs.rbp; }
  uint64_t shadow_call_sp() const { return 0; }

#endif

  zx_thread_state_general_regs_t regs;
  zx_exception_context_t exc;
};

// This is the type of the exception handler entry point.  It's always running
// on the phys_exception stack, freshly started from the top.  (Exceptions
// cannot meaningfully nest.)  In can use the full normal C++ ABI with unsafe
// and/or shadow call stacks, which likewise start fresh on the separate
// phys_exception_*_stack.
//
// Ordinarily this will not return at all.  If it does return, then it must
// return the PHYS_EXCEPTION_RESUME magic value.
using PhysExceptionHandler = uint64_t(uint64_t vector, const char* vector_name,
                                      PhysExceptionState& exception_state);

// This prints out register values and backtrace and such.
PHYS_SINGLETHREAD void PrintPhysException(uint64_t vector, const char* vector_name,
                                          const PhysExceptionState& state);

// This is called from assembly code by the default exception handlers.  This
// tells the assembly code to restore the register state from *regs and resume
// the interrupted state, or it never returns.
PHYS_SINGLETHREAD extern "C" PhysExceptionHandler PhysException;

// This indicates the (sole) expected exception.  PhysException will hand off
// to this handler in case the interrupted PC matches this exact value.
// Otherwise it will call PrintPhysException and then ArchPanicReset.
struct PhysHandledException {
  uintptr_t pc = 0;
  PhysExceptionHandler* handler = nullptr;
};

// This can be set to expect an exception.  It's always reset by PhysException.
extern PhysHandledException gPhysHandledException;

// This can be tail-called by a handler to change the special register values
// and resume execution.  It always returns PHYS_EXCEPTION_RESUME.  A handler
// that doesn't need to modify these special registers can just return
// PHYS_EXCEPTION_RESUME directly after modifying other registers in regs.
uint64_t PhysExceptionResume(PhysExceptionState& regs, uint64_t pc, uint64_t sp, uint64_t psr);

#endif  // !__ASSEMBLER__

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_EXCEPTION_H_
