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
    push_value $USER_DATA_SELECTOR    # ss
    push_value %rdx                   # sp
    push_value %r8                    # rflags
    push_value $USER_CODE_64_SELECTOR # cs
    push_value %rcx                   # pc

    ALL_CFI_UNDEFINED

    // Clear registers
    xorl %eax, %eax /* set %rax = 0 */
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    // Don't clear rdi or rsi, since they have the user arguments.
    xorl %ebp, %ebp
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d

    // We do not need to clear extended register state, since the kernel only
    // uses the general purpose registers, and the extended state is initialized
    // to a cleared state.

    swapgs

    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    iretq
END_FUNCTION(x86_uspace_entry)
