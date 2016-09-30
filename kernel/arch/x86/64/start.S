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
#include <arch/x86/registers.h>

/* shared code to set up a default 64bit page table structure */
.macro page_table_init
    /* load the physical pointer to the top level page table */
    movl $PHYS(pml4), %eax

    /* Setting the First PML4E with a PDP table reference*/
    movl $PHYS(pdp), %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, PHYS(pml4)

    /* Setting the First PDPTE with a Page table reference*/
    movl $PHYS(pte), %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, PHYS(pdp)

    /* point the pml4e at the second high PDP (for -2GB mapping) */
    movl $PHYS(pdp_high),   %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, PHYS(pml4 + 8*511)

    /* point the second pdp at the same low level page table */
    movl $PHYS(pte), %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax
    movl %eax, PHYS(pdp_high + 8*510)

    /* map the first 1GB in this table */
    movl $PHYS(pte), %esi
    movl $0x200, %ecx
    xor  %eax, %eax

0:
    mov  %eax, %ebx
    shll $21, %ebx
    orl  $X86_KERNEL_PD_LP_FLAGS, %ebx
    movl %ebx, (%esi)
    addl $8,%esi
    inc  %eax
    loop 0b

    /* set up a linear map of the first 64GB at 0xffffff8000000000 */
    movl $PHYS(linear_map_pdp), %esi
    movl $32768, %ecx
    xor  %eax, %eax

    /* loop across these page tables, incrementing the address by 2MB */
0:
    mov  %eax, %ebx
    shll $21, %ebx
    orl  $X86_KERNEL_PD_LP_FLAGS, %ebx    # lower word of the entry
    movl %ebx, (%esi)
    mov  %eax, %ebx
    shrl $11, %ebx      # upper word of the entry
    movl %ebx, 4(%esi)
    addl $8,%esi
    inc  %eax
    loop 0b

    /* point the high pdp at our linear mapping page tables */
    movl $PHYS(pdp_high), %esi
    movl $64, %ecx
    movl $PHYS(linear_map_pdp), %eax
    orl  $X86_KERNEL_PD_FLAGS, %eax

0:
    movl %eax, (%esi)
    add  $8, %esi
    addl $4096, %eax
    loop 0b
.endm

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

    /* load our data selectors */
    movw $DATA_SELECTOR, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* We need to jump to our sane 32 bit CS */
    pushl $CODE_SELECTOR
    pushl $PHYS(.Lfarjump)
    lret

.Lfarjump:
    /* zero the bss section */
    bss_setup

paging_setup:
    /* Preparing 64 bit paging, we will use 2MB pages covering 1GB
     * for initial bootstrap, this page table will be 1 to 1
     */

    /* Set the PAE bit to enable 64bit paging
     * Set PGE to enable global kernel pages
     */
    mov %cr4, %eax
    or $(X86_CR4_PAE|X86_CR4_PGE), %eax
    mov %eax, %cr4

    /* Long Mode Enabled at this point */
    movl $X86_MSR_EFER, %ecx
    rdmsr
    orl $X86_EFER_LME,%eax
    wrmsr

    /* initialize the default page tables */
    page_table_init

    /* load the physical pointer to the top level page table */
    movl $PHYS(pml4), %eax
    mov %eax, %cr3

    /* Enabling Paging and from this point we are in
    32 bit compatibility mode*/
    mov %cr0,  %eax
    btsl $(31), %eax
    mov %eax,  %cr0

    /* Start using the magenta stack from its physical address. */
    mov $PHYS(_kstack + 4096), %esp

    /* Using another long jump to be on 64 bit mode
    after this we will be on real 64 bit mode */
    pushl $CODE_64_SELECTOR     /*Need to put it in a the right CS*/
    pushl $PHYS(farjump64)
    lret

.align 8
.code64
farjump64:
    /* branch to our high address */
    mov  $high_entry, %rax
    jmp  *%rax

high_entry:
    /* zero our kernel segment data registers */
    xor %eax, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss

    /* load the high kernel stack */
    mov  $(_kstack + 4096), %rsp

    /* reload the gdtr */
    lgdt _gdtr

    /* set up the idt */
    mov $_idt, %rdi
    call idt_setup
    lidt _idtr

    /* assign this core CPU# 0 and initialize its per cpu state */
    mov $0, %rdi
    call x86_init_percpu

    /* call the main module */
    call lk_main

0:                          /* just sit around waiting for interrupts */
    hlt                     /* interrupts will unhalt the processor */
    pause
    jmp 0b                  /* so jump back to halt to conserve power */

    /* 64bit entry point from a secondary loader */
.align 8
.org 0x200
FUNCTION(_entry64)
    mov %esi, PHYS(_zero_page_boot_params)

    /* ensure the stack pointer is sane */

    /* This is enough space for the 2x push + lretq below.
     * After, we switch to _kstack + 4096.
     * But for some reason using _kstack + 4096 here causes
     * crashes on some UEFI platforms...
     */
    mov $PHYS(farjump64), %rsp

    /* zero the bss */
    bss_setup

.Lpaging_setup64:
    /* initialize the default page tables */
    page_table_init

    /*
     * Set PGE to enable global kernel pages
     */
    mov   %cr4, %rax
    or    $(X86_CR4_PGE), %rax
    mov   %rax, %cr4

    /* load the physical pointer to the top level page table */
    mov  $PHYS(pml4), %rax
    mov  %rax, %cr3

    /* load our gdtr */
    lgdt _gdtr

    /* long jump to our code selector and the high address */
    push  $CODE_64_SELECTOR
    push  $high_entry
    lretq
