// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>

/* Register use in this code:
 * Callee save:
 * %rbx = smap_avail
 * %r12 = dst
 * %r13 = src
 * %r14 = len
 * %r15 = fault_return
 */

.macro begin_usercopy
    push_reg %r12
    push_reg %r13
    push_reg %r14
    push_reg %r15
    push_reg %rbx

    # shift all of the arguments into callee save registers
    mov %rdi, %r12
    mov %rsi, %r13
    mov %rdx, %r14
    mov %r8, %r15
    mov %rcx, %rbx

    # Check if SMAP is enabled
    cmp $0, %rbx
    # Disable SMAP protection if SMAP is enabled
    jz 0f
    stac
0:
.endm

.macro end_usercopy
    # Re-enable SMAP protection
    cmp $0, %rbx
    jz 0f
    clac
0:
    pop_reg %rbx
    pop_reg %r15
    pop_reg %r14
    pop_reg %r13
    pop_reg %r12
.endm

# status_t _x86_copy_to_or_from_user(void *dst, const void *src, size_t len, bool smap, void **fault_return)
FUNCTION(_x86_copy_to_or_from_user)
    begin_usercopy

    # Setup page fault return
    movq $.Lfault_copy, (%r15)

    # Between now and the reset of the fault return, we cannot make a function
    # call or manipulate the stack.  We need to be able to restore all callee
    # registers, without any knowledge of where between these two points we
    # faulted.

    # Perform the actual copy
    cld
    mov %r12, %rdi
    mov %r13, %rsi
    mov %r14, %rcx
    rep movsb

    mov $MX_OK, %rax
    jmp .Lcleanup_copy

.Lfault_copy:
    mov $MX_ERR_INVALID_ARGS, %rax
.Lcleanup_copy:
    # Reset fault return
    movq $0, (%r15)

    end_usercopy
    ret
END_FUNCTION(_x86_copy_to_or_from_user)
