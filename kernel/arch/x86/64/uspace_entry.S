// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/descriptor.h>

# x86_uspace_entry(void* thread_arg, vaddr_t ustack, uint64_t rflags, vaddr_t entry_point)
FUNCTION(x86_uspace_entry)
    /* push a fake 64bit interrupt stack frame and iret to it */
    pushq $USER_DATA_SELECTOR    # ss
    pushq %rsi                   # ustack
    pushq %rdx                   # rflags
    pushq $USER_CODE_64_SELECTOR # cs
    pushq %rcx                   # entry

    # Clear registers
    xor %rax, %rax
    xor %rbx, %rbx
    xor %rcx, %rcx
    xor %rdx, %rdx
    xor %rsi, %rsi
    # Don't clear rdi, since it has the thread arg
    xor %rbp, %rbp
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15

    # TODO(teisenbe): Clear xmm/ymm stuff.  Currently this is all
    # handled by lazy FPU loading, but once we support YMM this will
    # likely change
    swapgs

    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    iretq
