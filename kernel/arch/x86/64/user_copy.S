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
    push %r12
    push %r13
    push %r14
    push %r15
    push %rbx

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
    pop %rbx
    pop %r15
    pop %r14
    pop %r13
    pop %r12
.endm

# status_t _x86_copy_from_user(void *dst, const void *src, size_t len, bool smap, void **fault_return)
FUNCTION(_x86_copy_from_user)
    begin_usercopy

    # Check that userspace has access to the [src, src+len) buffer
    mov %r13, %rdi
    mov %r14, %rsi
    call _x86_usercopy_can_read
    cmp $0, %rax
    jnz 0f
    mov $MX_ERR_INVALID_ARGS, %rax
    jmp .Lcleanup_copy_from
0:
    # Setup page fault return
    movq $.Lfault_copy_from, (%r15)

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
    jmp .Lcleanup_copy_from

.Lfault_copy_from:
    mov $MX_ERR_INVALID_ARGS, %rax
.Lcleanup_copy_from:
    # Reset fault return
    movq $0, (%r15)

    end_usercopy
    ret

# status_t _x86_copy_to_user(void *dst, const void *src, size_t len, bool smap, void **fault_return)
FUNCTION(_x86_copy_to_user)
    begin_usercopy

    # Check that userspace has access to the [dst, dst+len) buffer
    mov %r12, %rdi
    mov %r14, %rsi
    call _x86_usercopy_can_write
    cmp $0, %rax
    jnz 0f
    mov $MX_ERR_INVALID_ARGS, %rax
    jmp .Lcleanup_copy_to
0:
    # Setup page fault return
    movq $.Lfault_copy_to, (%r15)

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
    jmp .Lcleanup_copy_to

.Lfault_copy_to:
    mov $MX_ERR_INVALID_ARGS, %rax
.Lcleanup_copy_to:
    # Reset fault return
    movq $0, (%r15)

    end_usercopy
    ret
