// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/ticks.h>
#include <zircon/tls.h>

#include "zircon/kernel/phys/stack.h"

// This is the entry point from the boot loader or thereabouts.
// It receives one argument, in x0, usually a pointer (physical address).
// The caches and MMU are disabled.
//
// In a ZBI executable, this is where zbi_kernel_t::entry points and
// x0 holds the address of the data ZBI.
//
// In a boot shim, the header code jumps here after normalizing machine state
// and x0 holds what's usually a Device Tree address.
.function _start, global
  // As early as possible collect the time stamp.  x1 and x2 will be the
  // second argument to PhysMain, the arch::EarlyTicks structure passed in
  // two registers.  The first argument is already in x0.
  sample_ticks x1, x2, x9, x10, x11

  // Clear return address and frame pointer: at the root of the call stack.
  mov x29, xzr
  mov x30, xzr

  // Clear .bss.  Note this assumes it's aligned to 16, which is ensured
  // by BOOT_STACK_ALIGN (and other alignments in .bss) and the linker script.
  adr_global x9, _edata
  adr_global x10, _end
0:
  stp xzr, xzr, [x9], #16
  cmp x9, x10
  blt 0b

  // Set up the stacks and the thread pointer area.
  tp .req x9
  adr_global tp, boot_thread_pointer
  msr tpidr_el1, tp

  // Stack guard canary value.  The only "randomness" readily available is
  // our own load address, so swizzle that in with some arbitrary bits.
  boot_stack_guard .req x10
  movlit boot_stack_guard, 0xdeadbeef1ee2d00d
  eor boot_stack_guard, tp, boot_stack_guard

#if __has_feature(safe_stack)
  boot_unsafe_stack_ptr .req x11
  adr_global boot_unsafe_stack_ptr, boot_unsafe_stack + BOOT_STACK_SIZE
#else
  boot_unsafe_stack_ptr .req xzr
#endif

#if ZX_TLS_STACK_GUARD_OFFSET + 8 != ZX_TLS_UNSAFE_SP_OFFSET
#error "TLS ABI layout???"
#endif
  stp boot_stack_guard, boot_unsafe_stack_ptr, [tp, #ZX_TLS_STACK_GUARD_OFFSET]

  adr_global x12, boot_stack + BOOT_STACK_SIZE
  mov sp, x12

#if __has_feature(shadow_call_stack)
  // Shadow call stack grows up.
  adr_global shadow_call_sp, boot_shadow_call_stack
#endif

  // Now the full C++ ABI is available.  This could theoretically be a tail
  // call since it's obliged never to return, but it's nice to have the
  // caller in a backtrace.
  bl PhysMain

  // Trap forever just in case it does return.
0:
  brk #1
  b 0b
.end_function

.object boot_thread_area, bss, local, align=8
  .skip (-ZX_TLS_STACK_GUARD_OFFSET)
#if ZX_TLS_UNSAFE_SP_OFFSET < ZX_TLS_STACK_GUARD_OFFSET
  .error "TLS ABI layout??"
#endif
boot_thread_pointer:
.end_object
