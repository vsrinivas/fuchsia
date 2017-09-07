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
 * %rcx = argument 4, void** fault_return
 *   - moved to %r10
 */

# status_t _x86_copy_to_or_from_user(void *dst, const void *src, size_t len, void **fault_return)
FUNCTION(_x86_copy_to_or_from_user)
    # Copy fault_return out of %rcx, because %rcx is used by "rep movsb" later.
    movq %rcx, %r10

    # Check if SMAP is enabled
    cmpb $0, g_x86_feature_smap(%rip)
    # Disable SMAP protection if SMAP is enabled
    jz 0f
    stac
0:

    # Setup page fault return
    movq $.Lfault_copy, (%r10)

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

.Lcleanup_copy:
    # Reset fault return
    movq $0, (%r10)

    # Re-enable SMAP protection
    cmpb $0, g_x86_feature_smap(%rip)
    jz 0f
    clac
0:
    ret

.Lfault_copy:
    mov $MX_ERR_INVALID_ARGS, %rax
    jmp .Lcleanup_copy
END_FUNCTION(_x86_copy_to_or_from_user)
