//
// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include <asm.h>

/*
struct x86_64_registers {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rsp, rip;
};
extern ACPI_STATUS x86_do_suspend(struct x86_64_registers* registers);
*/
.code64
.section .text
FUNCTION(x86_do_suspend)
    # TODO(teisenbe): We need to also save floating point registers
    mov %rdi, (%rdi)
    mov %rsi, 8(%rdi)
    mov %rbp, 16(%rdi)
    mov %rbx, 24(%rdi)
    mov %rdx, 32(%rdi)
    mov %rcx, 40(%rdi)
    # Stash 0 as rax, so we return AE_OK if we do suspend
    movq $0, 48(%rdi)
    mov %r8, 56(%rdi)
    mov %r9, 64(%rdi)
    mov %r10, 72(%rdi)
    mov %r11, 80(%rdi)
    mov %r12, 88(%rdi)
    mov %r13, 96(%rdi)
    mov %r14, 104(%rdi)
    mov %r15, 112(%rdi)
    mov %rsp, 120(%rdi)
    movq $.Lafter_sleep, 128(%rdi)

    # Transition to S3
    mov $3, %rdi
    sub $8, %rsp
    call AcpiEnterSleepState
    add $8, %rsp
.Lafter_sleep:
    ret
