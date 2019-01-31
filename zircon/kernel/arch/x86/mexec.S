// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/defines.h>
#include <arch/x86/asm.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/registers.h>
#include <asm.h>
#include <zircon/tls.h>
#include <mexec.h>

#define CODE_SEGMENT_SELECTOR (1 << 3)
#define DATA_SEGMENT_SELECTOR (2 << 3)

.text
FUNCTION_LABEL(mexec_asm)
    // Make sure interrupts are disabled.
    cli

    /* Stash all the arguments passed in registers R8 - R13 */
    mov %r9,  %r13   /* Unused Arg */
    mov %r8,  %r12   /* Memmove Ops */
    mov %rcx, %r11   /* Unused Arg */
    mov %rdx, %r10   /* ENTRY64_ADDR */
    mov %rsi, %r9    /* CR3 for Safe page tables */
    mov %rdi, %r8    /* Bootimage Address */

    // The old SP is in the old kernel virtual address space, so don't use it.
    xor %esp, %esp

    // Make sure old PGE mappings from the kernel address space are not
    // still in the TLB.  Having them there masked the previous bug wherein
    // this code relied on using the incoming stack pointer.
    mov %cr4, %rax
    and $~X86_CR4_PGE, %rax
    mov %rax, %cr4

    // Switch to the safe identity mapped page tables.
    mov  %r9, %cr3

    // Load our little GDT defined below.  The current GDT is somewhere
    // that might be overwritten when we copy in the new kernel below.
    lea mexec_gdt(%rip), %rax
    mov %rax, mexec_gdt_pointer(%rip)
    lgdt mexec_gdt_descriptor(%rip)

    // Switch to the new data segment.
    mov $DATA_SEGMENT_SELECTOR, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    // Switch to the new code segment.
    // Note that ljmp accepts only a 32-bit address (aka "offset").
    // That's fine here, since we know we're running in the low 4G here.
    leal .Lnew_cs(%rip), %eax
    movl %eax, mexec_ljmp_descriptor(%rip)
    ljmp *mexec_ljmp_descriptor(%rip)
.Lnew_cs:

    /* Load the kernel relocation op into ram */
    mov MEMMOV_OPS_DST_OFFSET (%r12), %rdi
    mov MEMMOV_OPS_SRC_OFFSET (%r12), %rsi
    mov MEMMOV_OPS_LEN_OFFSET (%r12), %rcx

    /* Move RCX bytes from RSI to RDI */
    cld               /* Clear the direction flag so that we're copying forward */
                      /* by default when we start */

    cmp %rsi, %rdi    /* Compare the src and dst registers to see if we need to */
                      /* copy forwards or backwards */

    jbe .Ldo_copy      /* if dst is greater than src, go ahead and do the copy */
                      /* forwards */

    mov %rcx, %rax    /* rcx and rax contain the number of bytes to be copied */
    sub $1,   %rax    /* Move rsi and rdi to the end of their respective buffers */
    add %rax, %rdi
    add %rax, %rsi

    std               /* Set the direction flag to 1. This will ensure that the */
                      /* copy happens from the back of the buffers to the front */

.Ldo_copy:

    rep movsb         /* copy RCX bytes from RSI to RDI */

    cld               /* Clear the direction flag since we may have polluted it */
                      /* if we did a copy backwards */

    /* Move the address of the bootdata into the appropriate register */
    mov %r8, %rsi

    /* Zero out some registers */
    xor %ebx, %ebx
    xor %edi, %edi
    xor %ebp, %ebp

    /* Grab 64bit entrypoint from provided location */
    mov (%r10), %rax

    /* See you on the other side! */
    jmp *%rax

    /* Crash, we should never reach here */
    ud2

END_DATA(mexec_asm)

.balign 8
LOCAL_DATA(mexec_gdt)
    // Null entry.
    .int 0
    .int 0

    // 64-bit code segment.
    .short 0xffff           // limit 15:00
    .short 0x0000           // base 15:00
    .byte  0x00             // base 23:16
    .byte  0b10011010       // P(1) DPL(00) S(1) 1 C(0) R(1) A(0)
    .byte  0b10101111       // G(1) D(0) L(1) AVL(0) limit 19:16
    .byte  0x0              // base 31:24

    // Data segment.
    .short 0xffff           // limit 15:00
    .short 0x0000           // base 15:00
    .byte  0x00             // base 23:16
    .byte  0b10010010       // P(1) DPL(00) S(1) 0 E(0) W(1) A(0)
    .byte  0b11001111       // G(1) B(1) 0 0 limit 19:16
    .byte  0x0              // base 31:24
END_DATA(mexec_gdt)
DATA(mexec_gdt_end)

.balign 8
LOCAL_DATA(mexec_gdt_descriptor)
    .short mexec_gdt_end - mexec_gdt - 1
LOCAL_DATA(mexec_gdt_pointer)
    .quad 0 // Filled in at runtime.
END_DATA(mexec_gdt_descriptor)

.balign 8
LOCAL_DATA(mexec_ljmp_descriptor)
    .long 0 // Filled in at runtime.
    .short CODE_SEGMENT_SELECTOR
END_DATA(mexec_ljmp_descriptor)

DATA(mexec_asm_end)
