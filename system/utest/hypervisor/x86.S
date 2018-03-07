// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

#define IA32_GS_BASE              0xc0000101
#define INTERRUPT_ADDR(base, x)   (x - base + GUEST_ENTRY)
#define CODE_64_SELECTOR          0x8

.text

.macro init_interrupt_handling start
    lgdt INTERRUPT_ADDR(\start, gdtr_for_\start)
    lidt INTERRUPT_ADDR(\start, idtr_for_\start)
    mov $GUEST_ENTRY + 0x1000, %rsp
    jmp end_of_\start

int32_for_\start:
    movq $0, (EXIT_TEST_ADDR)

.align 8
gdt_for_\start:
    // Null entry.
    .8byte 0
    // CODE_64_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b10011010                               // P(1) DPL(00) S(1) 1 C(0) R(1) A(0)
    .byte  0b10101111                               // G(1) D(0) L(1) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
gdtr_for_\start:
    .2byte gdtr_for_\start - gdt_for_\start - 1
    .8byte INTERRUPT_ADDR(\start, gdt_for_\start)

.align 8
idt_for_\start:
    .fill 64, 8, 0                                  // 32 null entries
    .2byte INTERRUPT_ADDR(\start, int32_for_\start) // Offset 15:00
    .2byte CODE_64_SELECTOR                         // Segment selector
    .byte  0                                        // IST(000)
    .byte  0b10001110                               // P(1) DPL(00) 0 Type(1110)
    .2byte 0                                        // Offset 31:16
    .4byte 0                                        // Offset 63:32
    .4byte 0                                        // Reserved
idtr_for_\start:
    .2byte idtr_for_\start - idt_for_\start - 1     // Limit
    .8byte INTERRUPT_ADDR(\start, idt_for_\start)   // Address
end_of_\start:
.endm

// Test vcpu_resume.
FUNCTION(vcpu_resume_start)
    // Test that we do not exit on load/store of CR3.
    mov %cr3, %rax
    mov %rax, %cr3

    // Test that we do not exit on store of GS_BASE.
    xor %eax, %eax
    xor %edx, %edx
    mov $IA32_GS_BASE, %ecx
    wrmsr

    // Test that we handle CPUID instruction correctly.
    xor %eax, %eax
    cpuid

    movq $0, (EXIT_TEST_ADDR)
FUNCTION(vcpu_resume_end)

// Test vcpu_interrupt.
FUNCTION(vcpu_interrupt_start)
    init_interrupt_handling vcpu_interrupt_start
    sti
    jmp .
FUNCTION(vcpu_interrupt_end)

// Test guest HLT instruction handling.
FUNCTION(vcpu_hlt_start)
    init_interrupt_handling vcpu_hlt_start
    sti
    hlt
FUNCTION(vcpu_hlt_end)

// Test that pause exiting works correctly.
FUNCTION(vcpu_pause_start)
    pause
    movq $0, (EXIT_TEST_ADDR)
FUNCTION(vcpu_pause_end)

// Test that reading and writing cr0 works correctly.
FUNCTION(vcpu_write_cr0_start)
    // Read cr0.
    mov %cr0, %rax

    // Write cr0 and negate the NE bit.
    and $~X86_CR0_NE, %rax
    mov %rax, %cr0

    // Test cr0 masking. NE bit should be set.
    mov %cr0, %rax

    movq $0, (EXIT_TEST_ADDR)
FUNCTION(vcpu_write_cr0_end)

// Test vcpu_read_state and vcpu_write_state.
FUNCTION(vcpu_read_write_state_start)
    add $1, %rax
    add $2, %rcx
    add $3, %rdx
    add $4, %rbx
    add $5, %rsp
    add $6, %rbp
    add $7, %rsi
    add $8, %rdi
    add $9, %r8
    add $10, %r9
    add $11, %r10
    add $12, %r11
    add $13, %r12
    add $14, %r13
    add $15, %r14
    add $16, %r15

    stc  // Set carry flag (bit 0)
    stac // Set AC flag (bit 18)

    movq $0, (EXIT_TEST_ADDR)
FUNCTION(vcpu_read_write_state_end)

// Test guest_set_trap using a memory-based trap.
FUNCTION(guest_set_trap_start)
    movq $0, (TRAP_ADDR)
    movq $0, (EXIT_TEST_ADDR)
FUNCTION(guest_set_trap_end)

// Test guest_set_trap using an IO-based trap.
FUNCTION(guest_set_trap_with_io_start)
    out %al, $TRAP_PORT
    movq $0, (EXIT_TEST_ADDR)
FUNCTION(guest_set_trap_with_io_end)
