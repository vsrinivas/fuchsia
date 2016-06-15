// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

# This file provides real-mode entry points for
# 1) secondary CPU initialization

#include <asm.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/registers.h>
#include <arch/defines.h>

.align PAGE_SIZE

.section .text
FUNCTION(x86_bootstrap16_start)

.code16
FUNCTION(x86_bootstrap16_entry)
    # We cheat a little and don't switch off of our real mode segments in
    # protected mode.  In real mode and protected mode, all of our code
    # and data accesess are relative to %cs and %ss, using the real mode
    # segment calculations.

    # setup %ds/%ss to refer to the data region
    mov %cs, %si
    add $0x100, %si
    mov %si, %ds
    mov %si, %ss

    lgdtl BCD_PHYS_GDTR_OFFSET

    # enter protected mode (but without paging)
    mov %cr0, %ebx
    or $X86_CR0_PE, %ebx
    mov %ebx, %cr0

    # clear instruction prefetch queue
    jmp 0f
0:
    # enable PAE
    mov %cr4, %ecx
    or $X86_CR4_PAE, %ecx
    mov %ecx, %cr4

    # load CR3 with the bootstrap PML4
    mov BCD_PHYS_BOOTSTRAP_PML4_OFFSET, %ecx
    mov %ecx, %cr3

    # enable IA-32e mode and indicate support for NX pages.
    # need the latter for once we switch to the real kernel
    # address space.
    mov $X86_MSR_EFER, %ecx
    rdmsr
    or $X86_EFER_LME, %eax
    or $X86_EFER_NXE, %eax
    wrmsr

    # enable paging
    mov %cr0, %ebx
    or $X86_CR0_PG, %ebx
    mov %ebx, %cr0

    # Translate data page segment into full address
    mov %ds, %esi
    shl $4, %esi

    # Jump to 64-bit CS
    mov $BCD_PHYS_LM_ENTRY_OFFSET, %esp
    lretl

# Get the secondary cpu into 64-bit mode with interrupts disabled and no TSS
.code64
FUNCTION(_x86_secondary_cpu_long_mode_entry)
    # When we get here, %rsi should contain the absolute address of our data
    # page.
    mov $1, %rdi
    LOCK xadd %edi, BCD_CPU_COUNTER_OFFSET(%esi)
    # %rax is now the ID of this CPU

    # Retrieve this CPUs initial kernel stack
    # Note: the stack is unusable until we switch cr3 below
    mov %rdi, %rax
    dec %rax
    mov BCD_KSTACK_BASE_OFFSET(%rsi, %rax, 8), %rsp
    add $PAGE_SIZE, %rsp

    # Retrieve the new PML4 address before our data page becomes unreachable
    mov BCD_PHYS_KERNEL_PML4_OFFSET(%esi), %ecx
    # Similarly for the CPU waiting counter
    mov BCD_CPU_WAITING_OFFSET(%esi), %rdx

    # Switch out of the copied code page and into the kernel's
    # version of it
    mov  $.Lhighaddr, %rbx
    jmp  *%rbx
.Lhighaddr:
    # Switch to the kernel's PML4
    mov %rcx, %cr3
    # As of this point, %esi is invalid

    # Reload the GDT with one based off of non-identity mapping
    lgdt _gdtr

    # Zero our data segments
    xor %eax, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %fs
    mov %eax, %gs
    mov %eax, %ss

    # Load the IDT
    lidt _idtr

    mov %rdx, %rsi
    # Do an indirect call to keep this position independent
    # x86_secondary_entry(cpu_id, CPU ready counter)
    movq $x86_secondary_entry, %rbx
    call *%rbx

# If x86_secondary_entry returns, hang.
0:
    hlt
    jmp 0b

DATA(x86_bootstrap16_end)
    nop
