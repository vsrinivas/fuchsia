// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

#define IA32_GS_BASE    0xc0000101

.text

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
