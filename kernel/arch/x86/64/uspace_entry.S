// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/descriptor.h>

# x86_uspace_entry(uintptr_t arg1, uintptr_t arg2, uintptr_t sp, uintptr_t pc, uint64_t rflags)
FUNCTION(x86_uspace_entry)
    /* push a fake 64bit interrupt stack frame and iret to it */
    pushq $USER_DATA_SELECTOR    # ss
    pushq %rdx                   # sp
    pushq %r8                    # rflags
    pushq $USER_CODE_64_SELECTOR # cs
    pushq %rcx                   # pc

    // Clear registers
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    // Don't clear rdi or rsi, since they have the user arguments.
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15

    // We do not need to clear extended register state, since the kernel only
    // uses the general purpose registers, and the extended state is initialized
    // to a cleared state.

    swapgs

    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    iretq
