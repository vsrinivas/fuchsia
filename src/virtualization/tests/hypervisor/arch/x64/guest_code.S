// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/constants.h"
#include "src/virtualization/tests/hypervisor/asm.h"

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

#define CPUID_FEATURES_LEAF             1
// ECX
#define CPUID_FEATURE_MON_BIT           3
#define CPUID_FEATURE_VMX_BIT           5
#define CPUID_FEATURE_TM2_BIT           8
#define CPUID_FEATURE_PDCM_BIT          15
#define CPUID_FEATURE_X2APIC_BIT        21
#define CPUID_FEATURE_TSC_DEADLINE_BIT  24
#define CPUID_FEATURE_HYPERVISOR_BIT    31
// EDX
#define CPUID_FEATURE_TM_BIT            29
#define CPUID_FEATURE_ACPI_BIT          22
#define CPUID_FEATURE_SEP_BIT           11

// x2APIC constants
//
// See Intel Volume 3, Section 10.12.9: ICR Operation in x2APIC Mode

#define X2APIC_ICR_MSR 0x830                       // Interrupt Command Register MSR

#define X2APIC_ICR_SHORTHAND_NONE 0                // Use destination field
#define X2APIC_ICR_SHORTHAND_SELF 1                // Send IPI to self
#define X2APIC_ICR_SHORTHAND_ALL_INCLUDING_SELF 2  // Send IPI to all
#define X2APIC_ICR_SHORTHAND_ALL_EXCLUDING_SELF 3  // Send IPI to all but self

#define X2APIC_ICR_DEST_MODE_PHYSICAL 0
#define X2APIC_ICR_DELIVERY_MODE_FIXED 0
#define X2APIC_ICR_DELIVERY_MODE_NMI 4

#define X2APIC_ICR(shorthand, dest_mode, delivery_mode, vector)                        \
  (((shorthand) << 18) | ((dest_mode) << 11) | ((delivery_mode) << 8) | (vector))
#define X2APIC_ICR_FIXED(shorthand, vector)                        \
  X2APIC_ICR(shorthand, X2APIC_ICR_DEST_MODE_PHYSICAL, X2APIC_ICR_DELIVERY_MODE_FIXED, vector)
#define X2APIC_ICR_NMI(shorthand)                        \
  X2APIC_ICR(shorthand, 0, X2APIC_ICR_DELIVERY_MODE_NMI, 0)

.text

// Architecture-specific callbacks called by the FUNCTION macro.
.macro arch_function_start name start end
.endm
.macro arch_function_end name start end
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

// Test vcpu_enter.
FUNCTION vcpu_enter
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
END_FUNCTION

FUNCTION vcpu_wait
1:
    hlt
    jmp  1b
END_FUNCTION

// Test vcpu_read_state and vcpu_write_state.
FUNCTION vcpu_read_write_state
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
END_FUNCTION

// Test vcpu_interrupt.
FUNCTION vcpu_interrupt
    init_interrupt_handling vcpu_interrupt_start
    sti
    movq $0, (EXIT_TEST_ADDR)
    jmp .
END_FUNCTION

// Test guest_set_trap using a memory-based trap.
FUNCTION guest_set_trap
    movq $0, (TRAP_ADDR)
    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

// Test guest HLT instruction handling.
FUNCTION vcpu_hlt
    init_interrupt_handling vcpu_hlt_start
    sti
    hlt
END_FUNCTION

// Test that pause exiting works correctly.
FUNCTION vcpu_pause
    pause
    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

// Test that reading and writing cr0 works correctly.
FUNCTION vcpu_write_cr0
    // Read cr0.
    mov %cr0, %rax

    // Copy the initial value into rbx to be checked after exiting to the host.
    mov %rax, %rbx

    // Toggle NE and CD. These bits are in the guest/host mask.
    // Note: NW should already be clear, but CD=0 NW=1 is an invalid state.
    and $~(X86_CR0_NW | X86_CR0_CD), %rax
    or $X86_CR0_NE, %rax
    mov %rax, %cr0

    // Load cr0 to test masking and shadowing. NE, NW, and CD should be shadowed
    // and will be checked after exiting back to the host.
    mov %cr0, %rax

    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

// Test writing an invalid value to CR0.
FUNCTION vcpu_write_invalid_cr0
    init_interrupt_handling vcpu_write_invalid_cr0_start

    // Read cr0.
    mov %cr0, %rax

    // CD=0 NW=1 is an invalid state.
    and $~X86_CR0_CD, %rax
    or $X86_CR0_NW, %rax
    mov %rax, %cr0

    // The previous instruction should generate a GP.
    jmp .
END_FUNCTION

// Test 32 bit compatibility mode (long mode disabled).
FUNCTION vcpu_compat_mode
    init_gdt vcpu_compat_mode_start
    mov $0, %rbx
    mov $0, %rcx

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
END_FUNCTION

FUNCTION vcpu_syscall
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
END_FUNCTION

FUNCTION vcpu_sysenter
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
END_FUNCTION

FUNCTION vcpu_sysenter_compat
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
END_FUNCTION

FUNCTION vcpu_vmcall_invalid_number
    movq $(INVALID_HYPERCALL), %rax
    vmcall
    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

FUNCTION vcpu_vmcall_invalid_cpl
    init_interrupt_handling vcpu_vmcall_invalid_cpl_start

    xor %eax, %eax
    mov $((USER_CODE_64_SELECTOR << 16) | CODE_64_SELECTOR), %edx
    mov $X86_MSR_IA32_STAR, %ecx
    wrmsr

    pushfq
    mov $ABSOLUTE_ADDR(vcpu_vmcall_invalid_cpl_start, vcpu_vmcall_invalid_cpl_ring3), %rcx
    sysretq

vcpu_vmcall_invalid_cpl_ring3:
    movq $(INVALID_HYPERCALL), %rax
    vmcall
    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

FUNCTION vcpu_extended_registers
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
END_FUNCTION

// Test guest_set_trap using an IO-based trap.
FUNCTION guest_set_trap_with_io
    out %al, $TRAP_PORT
    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION

// A guest that just exits each time it is resumed.
FUNCTION vcpu_always_exit
    1:
    movq $0, (TRAP_ADDR)
    jmp  1b
END_FUNCTION

// A guest that sends a series of IPIs.
//
// The test Guest_VcpuIpi ensures that IPIs are send in the following order.
//
// Changes here will require the test to be updated too.
FUNCTION vcpu_ipi
    mov $X2APIC_ICR_MSR, %ecx

    // Send IPI to all
    xor %edx, %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_ALL_INCLUDING_SELF, INT_IPI_VECTOR), %eax
    wrmsr

    // Send IPI to self
    xor %edx, %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_SELF, INT_IPI_VECTOR), %eax
    wrmsr

    // Send IPI to others
    xor %edx, %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_ALL_EXCLUDING_SELF, INT_IPI_VECTOR), %eax
    wrmsr

    // Send IPI to CPU 2.
    mov $2, %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_NONE, INT_IPI_VECTOR), %eax
    wrmsr

    // Send IPI to broadcast destination (0xffff'ffff).
    mov $(-1), %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_NONE, INT_IPI_VECTOR), %eax
    wrmsr

    // Send IPI to invalid CPU 64.
    mov $64, %edx
    mov $X2APIC_ICR_FIXED(X2APIC_ICR_SHORTHAND_NONE, INT_IPI_VECTOR), %eax
    wrmsr

    // Send NMI all
    xor %edx, %edx
    mov $X2APIC_ICR_NMI(X2APIC_ICR_SHORTHAND_ALL_INCLUDING_SELF), %eax
    wrmsr

    // Send NMI self
    xor %edx, %edx
    mov $X2APIC_ICR_NMI(X2APIC_ICR_SHORTHAND_SELF), %eax
    wrmsr

    // Send NMI to others
    xor %edx, %edx
    mov $X2APIC_ICR_NMI(X2APIC_ICR_SHORTHAND_ALL_EXCLUDING_SELF), %eax
    wrmsr

    // Send NMI to CPU 2.
    mov $2, %edx
    mov $X2APIC_ICR_NMI(X2APIC_ICR_SHORTHAND_NONE), %eax
    wrmsr

    movq $0, (EXIT_TEST_ADDR)
END_FUNCTION


FUNCTION cpuid_features
    mov $CPUID_FEATURES_LEAF, %eax
    cpuid

.macro expect_feature_set reg, bit
    bt \bit, \reg
    jnc .Lcpuid_features_failure
.endm
.macro expect_feature_clr reg, bit
    bt \bit, \reg
    jc .Lcpuid_features_failure
.endm

    expect_feature_set %ecx, $CPUID_FEATURE_X2APIC_BIT
    expect_feature_set %ecx, $CPUID_FEATURE_TSC_DEADLINE_BIT
    expect_feature_set %ecx, $CPUID_FEATURE_HYPERVISOR_BIT
    expect_feature_clr %ecx, $CPUID_FEATURE_VMX_BIT
    expect_feature_clr %ecx, $CPUID_FEATURE_PDCM_BIT
    expect_feature_clr %ecx, $CPUID_FEATURE_MON_BIT
    expect_feature_clr %ecx, $CPUID_FEATURE_TM2_BIT

    expect_feature_set %edx, $CPUID_FEATURE_SEP_BIT
    expect_feature_clr %edx, $CPUID_FEATURE_TM_BIT
    expect_feature_clr %edx, $CPUID_FEATURE_ACPI_BIT

    // success
    movq $0, (EXIT_TEST_ADDR)
.Lcpuid_features_failure:
    movq $0, (EXIT_TEST_FAILURE_ADDR)
END_FUNCTION
