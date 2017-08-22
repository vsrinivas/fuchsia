// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>

/* Register use in this code:
 * %rdi = argument 1, void* dst
 * %rsi = argument 2, const void* src
 * %rdx = argument 3, size_t len
 *   - moved to %rcx
 * %rcx = argument 4, bool smap_avail
 *   - moved to %r10
 * %r8 = argument 5, void** fault_return
 */

.macro begin_usercopy
    # Copy smap_avail out of %rcx, because %rcx is used by "rep movsb" later.
    movl %ecx, %r10d

    # Check if SMAP is enabled
    cmpl $0, %r10d
    # Disable SMAP protection if SMAP is enabled
    jz 0f
    stac
0:
.endm

.macro end_usercopy
    # Re-enable SMAP protection
    cmpl $0, %r10d
    jz 0f
    clac
0:
.endm

# status_t _x86_copy_to_or_from_user(void *dst, const void *src, size_t len, bool smap, void **fault_return)
FUNCTION(_x86_copy_to_or_from_user)
    begin_usercopy

    # Setup page fault return
    movq $.Lfault_copy, (%r8)

    # Between now and the reset of the fault return, we cannot make a function
    # call or manipulate the stack.  We need to be able to restore all callee
    # registers, without any knowledge of where between these two points we
    # faulted.

    # Perform the actual copy
    cld
    # %rdi and %rsi already contain the destination and source addresses.
    movq %rdx, %rcx
    rep movsb  # while (rcx-- > 0) *rdi++ = *rsi++;

    mov $MX_OK, %rax
    jmp .Lcleanup_copy

.Lfault_copy:
    mov $MX_ERR_INVALID_ARGS, %rax
.Lcleanup_copy:
    # Reset fault return
    movq $0, (%r8)

    end_usercopy
    ret
END_FUNCTION(_x86_copy_to_or_from_user)
