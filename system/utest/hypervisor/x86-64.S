// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

#define FUNCTION(x)     .global x; .type x,STT_FUNC; x:
#define IA32_GS_BASE    0xc0000101

.text

FUNCTION(vcpu_resume_start)
    mov $UART_PORT, %dx
    mov $'m', %al
    out %al, %dx
    mov $'x', %al
    out %al, %dx

    mov %cr3, %rax
    mov %rax, %cr3

    xor %eax, %eax
    xor %edx, %edx
    mov $IA32_GS_BASE, %ecx
    wrmsr

    xor %eax, %eax
    cpuid

    out %al, $EXIT_TEST_PORT
FUNCTION(vcpu_resume_end)

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

    out %al, $EXIT_TEST_PORT
FUNCTION(vcpu_read_write_state_end)

FUNCTION(guest_set_trap_start)
    mov $VMO_SIZE, %rdx
    mov -0x1000(%rdx), %rax
FUNCTION(guest_set_trap_end)

FUNCTION(guest_set_trap_with_port_start)
    out %al, $TRAP_PORT
    out %al, $EXIT_TEST_PORT
FUNCTION(guest_set_trap_with_port_end)
