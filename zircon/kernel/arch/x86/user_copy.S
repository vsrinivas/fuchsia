// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>
#include <lib/code_patching.h>

#define STAC APPLY_CODE_PATCH_FUNC(fill_out_stac_instruction, 3)
#define CLAC APPLY_CODE_PATCH_FUNC(fill_out_clac_instruction, 3)

/* void _x86_usercopy_erms(void* dst, void* src, size_t size) */
FUNCTION(_x86_usercopy_erms)
    movq %rdx, %rcx
    rep movsb
.global _x86_usercopy_erms_end
_x86_usercopy_erms_end:
    ret
END_FUNCTION(_x86_usercopy_erms)

/* void _x86_usercopy_quad(void* dst, void* src, size_t size) */
FUNCTION(_x86_usercopy_quad)
    movq %rdx, %rcx
    shrq $3, %rcx
    rep movsq
    andl $7, %edx
    je 1f
    movl %edx, %ecx
    rep movsb
.global _x86_usercopy_quad_end
_x86_usercopy_quad_end:
1:  ret
END_FUNCTION(_x86_usercopy_quad)

/* Register use in this code:
 * %rdi = argument 1, void* dst
 * %rsi = argument 2, const void* src
 * %rdx = argument 3, size_t len
 *   - moved to %rcx
 * %rcx = argument 4, uint64_t* fault_return
 *   - moved to %r10
 */

// X64CopyToFromUserRet _x86_copy_to_or_from_user(void *dst, const void *src, size_t len, uint64_t *fault_return, uint64_t fault_return_mask)
FUNCTION(_x86_copy_to_or_from_user)
    // Copy fault_return out of %rcx, because %rcx is used by "rep movsb" later.
    movq %rcx, %r10

    // Setup page fault return
    leaq .Lfault_copy(%rip), %rax
    // Given that we are in the kernel and our page fault return address will be a kernel address
    // we can toggle the high bit of fault_return (which controls whether the hardware page fault
    // handler actually runs or not) by simpling ANDing with the supplied page_fault_mask.
    andq %r8, %rax
    movq %rax, (%r10)

    // Disable SMAP protection if SMAP is enabled
    STAC

    // Between now and the reset of the fault return, we cannot make a function
    // call or manipulate the stack.  We need to be able to restore all callee
    // registers, without any knowledge of where between these two points we
    // faulted.

    APPLY_CODE_PATCH_FUNC(x86_usercopy_select, 19)
    // Set eax to ZX_OK. In this case since we do not set rdx the value of the fault address in the
    // return struct is undefined.
    xorl %eax, %eax

.Lcleanup_copy:
    // Re-enable SMAP protection
    CLAC

    // Reset fault return
    movq $0, (%r10)
    ret

.Lfault_copy:
    // If we are capturing faults the flags will have been placed in rcx and the fault address in
    // rdx. In case we were capturing faults we shuffle the flags to get them into the high bits of
    // rax. It is up to the caller to know if fault capture was enabled and hence whether the flags
    // and fault address will be valid values.
    shlq $32, %rcx
    movl $ZX_ERR_INVALID_ARGS, %eax
    or %rcx, %rax
    jmp .Lcleanup_copy
END_FUNCTION(_x86_copy_to_or_from_user)
