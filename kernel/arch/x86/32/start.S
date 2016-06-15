// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/asm.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>

/* The magic number passed by a Multiboot-compliant boot loader. */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

.section ".text.boot"
.code32

FUNCTION(_protected_start)
    /* attempt to detect if %esi points to a zero page structure */
    cmp $0, %esi
    je .Lcommon_boot
    cmpl $0x53726448, 0x202(%esi)
    jne .Lcommon_boot
    mov %esi, PHYS(_zero_page_boot_params)
    jmp .Lcommon_boot

FUNCTION(_multiboot_start)
    cmpl $MULTIBOOT_BOOTLOADER_MAGIC, %eax
    jne .Lcommon_boot
    movl %ebx, PHYS(_multiboot_info)

.Lcommon_boot:
    /* load our new gdt by physical pointer */
    lgdt PHYS(_gdtr_phys)

    movw $DATA_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %ss
    movw %ax, %gs
    movw %ax, %ss

    /*We jumped here in protected mode in a code segment that migh not longer
      be valid , do a long jump to our code segment, we use lret instead of
      ljmp to be able to use relative labels */
    pushl $CODE_SELECTOR     /*Pushing our code segment */
    pushl $PHYS(.Lfarjump)   /*and jump address */
    lret    /*This instruction will jump to codesel:farjump */

.Lfarjump:
    /* zero the bss section */
    bss_setup

paging_setup:
#ifdef PAE_MODE_ENABLED
#error broken for now
    /* Preparing PAE paging, we will use 2MB pages covering 1GB
    for initial bootstrap, this page table will be 1 to 1 */

    /* Setting the First PDPTE with a PD table reference*/
    movl $pd,   %eax
    orl  $0x01, %eax
    movl %eax, (pdp)

    movl $pd, %esi
    movl $0x1ff, %ecx

fill_pd:
    movl $0x1ff, %eax
    subl %ecx, %eax
    shll $21,%eax
    orl  $0x83, %eax
    movl %eax, (%esi)
    addl $8,%esi
    loop fill_pd

    /* Set PDPT in CR3 */
    movl $pdp, %eax
    mov %eax, %cr3

    /* Enabling PAE*/
    mov %cr4, %eax
    btsl $(5), %eax
    mov %eax, %cr4

    /* Enabling Paging and from this point we are in
    32 bit compatibility mode */
    mov %cr0,  %eax
    btsl $(31), %eax
    mov %eax,  %cr0

#else
    /* map the first 1GB 1:1 */
    movl $PHYS(pd), %esi
    movl $0x100, %ecx
    xor  %eax, %eax

.Lfill_pd:
    mov  %eax, %edx
    orl  $X86_KERNEL_PD_LP_FLAGS, %edx
    movl %edx, (%esi)
    addl $4, %esi
    addl $0x00400000, %eax
    loop .Lfill_pd

    /* map the first 1GB to KERNEL_ASPACE_BASE */
    movl $(PHYS(pd) + 0x800), %esi
    movl $0x100, %ecx
    xor  %eax, %eax

.Lfill_pd2:
    mov  %eax, %edx
    orl  $X86_KERNEL_PD_LP_FLAGS, %edx
    movl %edx, (%esi)
    addl $4, %esi
    addl $0x00400000, %eax
    loop .Lfill_pd2

    /* Set PD in CR3 */
    movl $PHYS(pd), %eax
    mov %eax, %cr3

    /* Enabling Paging and from this point we are in */
    mov %cr4, %eax
    orl $0x10, %eax
    mov %eax, %cr4

    mov %cr0,  %eax
    btsl $(31), %eax
    mov %eax, %cr0
#endif

    /* load the high kernel stack */
    movl $(_kstack + 4096), %esp

    /* reload the high gdtr */
    lgdt PHYS(_gdtr)

    /* branch to the high address */
    movl $main_lk, %eax
    jmp *%eax

main_lk:
    /* set up the idt */
    push $_idt
    call idt_setup
    add $4, %esp
    lidt _idtr

    /* set up the per cpu gs: pointer */
    call x86_init_percpu

    /* call the main module */
    call lk_main
0:                          /* just sit around waiting for interrupts */
    hlt                     /* interrupts will unhalt the processor */
    pause
    jmp 0b                  /* so jump back to halt to conserve power */
