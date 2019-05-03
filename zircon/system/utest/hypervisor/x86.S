// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

#define IA32_GS_BASE              0xc0000101
#define X86_MSR_IA32_STAR         0xc0000081
#define X86_MSR_IA32_LSTAR        0xc0000082
#define X86_MSR_IA32_SYSENTER_CS  0x00000174
#define X86_MSR_IA32_SYSENTER_ESP 0x00000175
#define X86_MSR_IA32_SYSENTER_EIP 0x00000176
#define X86_MSR_IA32_EFER         0xc0000080
#define ABSOLUTE_ADDR(base, x)    (x - base + GUEST_ENTRY)
#define CODE_32_SELECTOR          (1<<3)
#define CODE_64_SELECTOR          (2<<3)
#define DATA_SELECTOR             (3<<3)
#define USER_CODE_32_SELECTOR     (4<<3)
#define USER_DATA_SELECTOR        (5<<3)
#define USER_CODE_64_SELECTOR     (6<<3)
#define CODE_16_SELECTOR          (8<<3)
#define INVALID_HYPERCALL         0xffffffff

.text

// Define new function 'name'.
// Local label '1:' used by other macros to compute offsets.
.macro FUNCTION name
    .global \name
    .type \name, STT_FUNC
    \name:
    1:
.endm

.macro init_gdt start
    lgdt ABSOLUTE_ADDR(\start, gdtr_for_\start)
    mov $GUEST_ENTRY + 0x1000, %rsp
    jmp end_of_init_gdt_for_\start

.align 8
gdt_for_\start:
    // Null entry.
    .8byte 0
    // CODE_32_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b10011010                               // P(1) DPL(00) S(1) 1 C(0) R(1) A(0)
    .byte  0b11001111                               // G(1) D(1) L(0) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
    // CODE_64_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b10011010                               // P(1) DPL(00) S(1) 1 C(0) R(1) A(0)
    .byte  0b10101111                               // G(1) D(0) L(1) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
    // DATA_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b10010010                               // P(1) DPL(00) S(1) 0 E(0) W(1) A(0)
    .byte  0b11001111                               // G(1) B(1) L(0) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
    // USER_CODE_32_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b11111010                               // P(1) DPL(11) S(1) 1 C(0) R(1) A(0)
    .byte  0b11001111                               // G(1) D(1) L(0) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
    // USER_DATA_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b11110010                               // P(1) DPL(11) S(1) 0 E(0) W(1) A(0)
    .byte  0b11001111                               // G(1) B(1) 0 0 limit 19:16
    .byte  0x0                                      // Base 31:24
    // USER_CODE_64_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b11111010                               // P(1) DPL(11) S(1) 1 C(0) R(1) A(0)
    .byte  0b10101111                               // G(1) D(0) L(1) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
    // USER_DATA_SELECTOR duplicate for sysexit
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b11110010                               // P(1) DPL(11) S(1) 0 E(0) W(1) A(0)
    .byte  0b11001111                               // G(1) B(1) 0 0 limit 19:16
    .byte  0x0                                      // Base 31:24
    // CODE_16_SELECTOR
    .2byte 0xffff                                   // Limit 15:00
    .2byte 0x0000                                   // Base 15:00
    .byte  0x00                                     // Base 23:16
    .byte  0b10011010                               // P(1) DPL(00) S(1) 1 C(0) R(1) A(0)
    .byte  0b10001111                               // G(1) D(0) L(0) AVL(0) Limit 19:16
    .byte  0x0                                      // Base 31:24
gdtr_for_\start:
    .2byte gdtr_for_\start - gdt_for_\start - 1
    .8byte ABSOLUTE_ADDR(\start, gdt_for_\start)
end_of_init_gdt_for_\start:
.endm

.macro isr start, n
int_\n\()_for_\start:
    mov $\n, %rax
    movq $0, (EXIT_TEST_ADDR)
.endm

.macro idt start, n
    .2byte ABSOLUTE_ADDR(\start, \
                         int_\n\()_for_\start)      // Offset 15:00
    .2byte CODE_64_SELECTOR                         // Segment selector
    .byte  0                                        // IST(000)
    .byte  0b10001110                               // P(1) DPL(00) 0 Type(1110)
    .2byte 0                                        // Offset 31:16
    .4byte 0                                        // Offset 63:32
    .4byte 0                                        // Reserved
.endm

.macro init_interrupt_handling start
    init_gdt \start
    lidt ABSOLUTE_ADDR(\start, idtr_for_\start)
    mov $GUEST_ENTRY + 0x1000, %rsp
    jmp end_of_\start

.irp n, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
       16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
    isr \start \n
.endr

.align 8
idt_for_\start:
.irp n, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, \
       16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
    idt \start \n
.endr

idtr_for_\start:
    .2byte idtr_for_\start - idt_for_\start - 1     // Limit
    .8byte ABSOLUTE_ADDR(\start, idt_for_\start)    // Address
end_of_\start:
.endm

// Test vcpu_resume.
FUNCTION vcpu_resume_start
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
FUNCTION vcpu_resume_end

// Test vcpu_read_state and vcpu_write_state.
FUNCTION vcpu_read_write_state_start
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
FUNCTION vcpu_read_write_state_end

// Test vcpu_interrupt.
FUNCTION vcpu_interrupt_start
    init_interrupt_handling vcpu_interrupt_start
    sti
    movq $0, (EXIT_TEST_ADDR)
    jmp .
FUNCTION vcpu_interrupt_end

// Test guest_set_trap using a memory-based trap.
FUNCTION guest_set_trap_start
    movq $0, (TRAP_ADDR)
    movq $0, (EXIT_TEST_ADDR)
FUNCTION guest_set_trap_end

// Test guest HLT instruction handling.
FUNCTION vcpu_hlt_start
    init_interrupt_handling vcpu_hlt_start
    sti
    hlt
FUNCTION vcpu_hlt_end

// Test that pause exiting works correctly.
FUNCTION vcpu_pause_start
    pause
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_pause_end

// Test that reading and writing cr0 works correctly.
FUNCTION vcpu_write_cr0_start
    // Read cr0.
    mov %cr0, %rax

    // Write cr0 and negate the NE bit.
    and $~X86_CR0_NE, %rax
    mov %rax, %cr0

    // Test cr0 masking. NE bit should be set.
    mov %cr0, %rax

    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_write_cr0_end

// Test 32 bit compatibility mode (long mode disabled).
FUNCTION vcpu_compat_mode_start
    init_gdt vcpu_compat_mode_start
    movq $0, %rbx
    movq $0, %rcx

    // Drop into compatibility mode
    pushq $CODE_32_SELECTOR
    lea vcpu_compat_mode_code(%rip),%rax
    pushq %rax
    lretq

.code32
vcpu_compat_mode_code:
    // Hack to check if we're x86 or x64. In x86, 0x41 is `inc %ecx` and loop
    // falls through. In x64, 0x41 is the REX.B prefix and %ecx is not
    // incremented so loop jmps to vcpu_compat_mode_exit.
    xor %ecx,%ecx
    .byte 0x41 // x86: inc %ecx; x64: rex.B prefix
    loop vcpu_compat_mode_exit

    // Write a value to ebx to check in guest.cpp
    mov $1,%ebx

    // Go back to long mode
    pushl $CODE_64_SELECTOR
    pushl $ABSOLUTE_ADDR(vcpu_compat_mode_start, vcpu_compat_mode_test16)
    lret
.code64

vcpu_compat_mode_test16:
    // Drop into compatibility mode with 16 bit default operands.
    pushq $CODE_16_SELECTOR
    lea vcpu_compat_mode_code16(%rip),%rax
    pushq %rax
    lretq

.code32
vcpu_compat_mode_code16:
    // The default operand size should be 16 bits and push should subtract 2
    // from the stack pointer.
    mov %esp,%eax
    sub $2,%eax
    push $0
    cmp %esp,%eax
    pop %eax
    jne vcpu_compat_mode_code16_done

    // Write a value to ebx to check in guest.cpp
    mov $2,%ecx

    // Go back to long mode
vcpu_compat_mode_code16_done:
    pushl $CODE_64_SELECTOR
    pushl $ABSOLUTE_ADDR(vcpu_compat_mode_start, vcpu_compat_mode_done)
    lret
.code64

vcpu_compat_mode_done:
    // Fix the data segment selectors
    mov $DATA_SELECTOR,%rax
    mov %rax,%ds
    mov %rax,%es
    mov %rax,%fs
    mov %rax,%gs
    mov %rax,%ss

vcpu_compat_mode_exit:
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_compat_mode_end

FUNCTION vcpu_syscall_start
    init_interrupt_handling vcpu_syscall_start

    xor %eax, %eax
    mov $((USER_CODE_32_SELECTOR << 16) | CODE_64_SELECTOR), %edx
    mov $X86_MSR_IA32_STAR, %ecx
    wrmsr

    xor %edx, %edx
    mov $ABSOLUTE_ADDR(vcpu_syscall_start, vcpu_syscall_done), %eax
    mov $X86_MSR_IA32_LSTAR, %ecx
    wrmsr

    pushfq
    pop %r11
    mov $ABSOLUTE_ADDR(vcpu_syscall_start, vcpu_syscall_ring3), %rcx
    sysretq

vcpu_syscall_ring3:
    syscall

vcpu_syscall_done:
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_syscall_end

FUNCTION vcpu_sysenter_start
    init_interrupt_handling vcpu_sysenter_start

    xor %edx, %edx
    mov $CODE_64_SELECTOR, %eax
    mov $X86_MSR_IA32_SYSENTER_CS, %ecx
    wrmsr

    mov $ABSOLUTE_ADDR(vcpu_sysenter_start, vcpu_sysenter_done), %eax
    mov $X86_MSR_IA32_SYSENTER_EIP, %ecx
    wrmsr

    mov %esp, %eax
    mov $X86_MSR_IA32_SYSENTER_ESP, %ecx
    wrmsr

    mov %rsp, %rdx
    mov $ABSOLUTE_ADDR(vcpu_sysenter_start, vcpu_sysenter_ring3), %rdx
    .byte 0x48 // rex.W
    sysexit

vcpu_sysenter_ring3:
    sysenter

vcpu_sysenter_done:
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_sysenter_end

FUNCTION vcpu_sysenter_compat_start
    init_interrupt_handling vcpu_sysenter_compat_start

    xor %edx, %edx
    mov $CODE_64_SELECTOR, %eax
    mov $X86_MSR_IA32_SYSENTER_CS, %ecx
    wrmsr

    mov $ABSOLUTE_ADDR(vcpu_sysenter_compat_start, vcpu_sysenter_compat_done), %eax
    mov $X86_MSR_IA32_SYSENTER_EIP, %ecx
    wrmsr

    mov %esp, %eax
    mov $X86_MSR_IA32_SYSENTER_ESP, %ecx
    wrmsr

    mov %rsp, %rdx
    mov $ABSOLUTE_ADDR(vcpu_sysenter_compat_start, vcpu_sysenter_compat_ring3), %rdx
    sysexit

.code32
vcpu_sysenter_compat_ring3:
    sysenter
.code64

vcpu_sysenter_compat_done:
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_sysenter_compat_end

FUNCTION vcpu_vmcall_start
    movq $(INVALID_HYPERCALL), %rax
    vmcall
    movq $0, (EXIT_TEST_ADDR)
FUNCTION vcpu_vmcall_end

FUNCTION vcpu_extended_registers_start
    // Enable SSE instructions.
    mov %cr4, %rax
    or $(X86_CR4_OSFXSR), %rax
    mov %rax, %cr4

    // Store data in xmm0.
    movdqu xmm_data(%rip), %xmm0

    movq $0, (EXIT_TEST_ADDR)

    // Read data back from xmm0 into rax:rbx.
    movq %xmm0, %rax
    movhlps %xmm0, %xmm0
    movq %xmm0, %rbx

    movq $0, (EXIT_TEST_ADDR)

    // Disable SSE instructions
    mov %cr4, %rax
    and $(~(X86_CR4_OSFXSR)), %rax
    mov %rax, %cr4

    movq $0, (EXIT_TEST_ADDR)
    // If we can get back here then the host correctly handled our sse state becoming reduced
    movq $0, (EXIT_TEST_ADDR)

.align 16
xmm_data:
    .4byte 0x01234567
    .4byte 0x89abcdef
    .4byte 0xfedcba98
    .4byte 0x76543210
FUNCTION vcpu_extended_registers_end

// Test guest_set_trap using an IO-based trap.
FUNCTION guest_set_trap_with_io_start
    out %al, $TRAP_PORT
    movq $0, (EXIT_TEST_ADDR)
FUNCTION guest_set_trap_with_io_end

