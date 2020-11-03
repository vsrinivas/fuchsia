// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <lib/arch/ticks.h>
#include <zircon/tls.h>

#include "phys32.h"
#include "zircon/kernel/phys/stack.h"

// An argument is in %esi.
.function Phys32Entry, global
  // As early as possible collect the time stamp into %eax and %edx.
  sample_ticks

  // Now save the arch::EarlyTicks in %ebx, %edx since %eax is needed for
  // the loop below.  The boot loader argument is still in %esi.
  mov %eax, %ebx

  // Clear .bss.  Note this assumes it's aligned to 4, which is ensured
  // by BOOT_STACK_ALIGN (and other alignments in .bss) and the linker script.
  cld
  xor %eax, %eax
  mov $_edata, %edi
  mov $_end, %ecx
  sub %edi, %ecx
  shr $2, %ecx
  rep stosl

  // Set up the machine stack.
  mov $(boot_stack + BOOT_STACK_SIZE), %esp

  // Clear the IDT to zero address and zero limit, so any trap is sure to
  // get a triple-fault.  We've just cleared the .bss containing the stack,
  // so these bytes are known to be zero.
  lidt -6(%esp)

  // Now we can use the stack for scratch space.  %eax is already zero
  // from above.  Use it to clear EFLAGS, since Multiboot makes few
  // guarantees about the initial state.
  push %eax
  .cfi_adjust_cfa_offset 4
  popf
  .cfi_adjust_cfa_offset -4

  // Set up our own GDT rather than continuing with what the boot loader
  // supplied.  This lets us use a %gs segment for the stack-safety ABIs.
  // It's also needed eventually to switch to 64-bit mode, so might as well.
  // Since 32-bit links are fixed-address, the contents of the GDT are in fact
  // all fixed at link time.  But the way the base address is split up is
  // not supported by any relocation types, so fill in the address at runtime.
  // Note gPhys32Gdt is declared as const in C++ and placed in RODATA, but
  // since there are no page protections anyway it's fine to just write it.
  mov $boot_thread_area, %eax
  mov %ax, gPhys32Gdt + PHYS32_GS_BASE_LO16_OFFSET
  shr $16, %eax
  mov %al, gPhys32Gdt + PHYS32_GS_BASE_MID8_OFFSET
  mov %ah, gPhys32Gdt + PHYS32_GS_BASE_HI8_OFFSET

  // Build a temporary descriptor pointing at the GDT to load it.
  movl $gPhys32Gdt, -4(%esp)
  movw $(PHYS32_GDT_SIZE - 1), -6(%esp)
  lgdt -6(%esp)

  // The GDT is in place, but we're still on the old incoming segments.
  // Jump to the new code segment and then reset the other segment registers.
  ljmp $PHYS32_CODE32_SEL, $0f
0:
  mov $PHYS32_GS32_SEL, %ax
  mov %ax, %gs
  mov $PHYS32_DATA32_SEL, %ax
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %ss
  xor %eax, %eax
  mov %ax, %fs

  // Clear frame pointer: at the root of the call stack.
  xor %ebp, %ebp

  push %eax  // Adjust down to maintain stack alignment after argument pushes.
  .cfi_adjust_cfa_offset 4
  push %esi  // First argument (1st word): void* (Multiboot or boot_params)
  .cfi_adjust_cfa_offset 4
  push %edx  // Second argument (2nd word): arch::EarlyTicks (low order)
  .cfi_adjust_cfa_offset 4
  push %ebx  // Second argument (3rd word): arch::EarlyTicks (high order)
  .cfi_adjust_cfa_offset 4

  // Initialize cpuid data.  This could be done earlier, but nothing needs it
  // and doing it here avoids saving and restoring any extra registers.
  call InitializeBootCpuid

  // Now the full C++ ABI is available.  This could theoretically be a tail
  // call since it's obliged never to return, but it's nice to have the
  // caller in a backtrace (and the call implicitly adjusts the stack
  // alignment as the ABI requires).
  call PhysMain

  // Pop the arguments (and padding) in the standard way (not that it matters).
  addl $4 * 4, %esp
  .cfi_adjust_cfa_offset -4 * 4

  // Trap forever just in case it does return.
0:
  ud2
  jmp 0b

.end_function

.object boot_thread_area, data, local, align=8
  .org boot_thread_area + ZX_TLS_STACK_GUARD_OFFSET
  .quad 0xdeadbeef1badd00d
  .org boot_thread_area + ZX_TLS_UNSAFE_SP_OFFSET
#if __has_feature(safe_stack)
  .long boot_unsafe_stack + BOOT_STACK_SIZE
#else
#endif
  .org boot_thread_area + ZX_TLS_UNSAFE_SP_OFFSET + 8
.end_object
