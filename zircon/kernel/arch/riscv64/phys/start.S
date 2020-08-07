// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/ticks.h>
#include <zircon/tls.h>

#include <phys/stack.h>

// This is the entry point from the boot loader or thereabouts.
// It receives one argument, in a0, usually a pointer (physical address).
// The caches and MMU are disabled.
//
// In a ZBI executable, this is where zbi_kernel_t::entry points and
// a0 holds the address of the data ZBI.
//
// In a boot shim, the header code jumps here after normalizing machine state
// and a0 holds what's usually a Device Tree address.
.function _start, global
  // Clear return address and frame pointer: at the root of the call stack.
  mv ra, x0
  mv fp, x0

  // Clear any incoming stack pointer so it can't be used accidentally
  // before the proper stack is set up below.
  mv sp, x0

  // Clear .bss.  Note this assumes it's aligned to 16, which is ensured
  // by BOOT_STACK_ALIGN (and other alignments in .bss) and the linker script.
1:
  lla t0, _edata
  lla t1, _end
0:
  sd zero, (t0)
  add t0, t0, 8
  bne t0, t1, 0b

  // Set up the stacks and the thread pointer area.
  lla sp, boot_stack
  li a1, BOOT_STACK_SIZE
  add sp, sp, a1
  lla tp, boot_thread_pointer

  // Now the full C++ ABI is available.  This could theoretically be a tail
  // call since it's obliged never to return, but it's nice to have the
  // caller in a backtrace.
//  jal PhysMain

  // Trap forever just in case it does return.
0:
  ebreak
  j 0b
.end_function

.object boot_thread_area, bss, local, align=8
  .skip (-ZX_TLS_STACK_GUARD_OFFSET)
#if ZX_TLS_UNSAFE_SP_OFFSET < ZX_TLS_STACK_GUARD_OFFSET
  .error "TLS ABI layout??"
#endif
boot_thread_pointer:
.end_object

