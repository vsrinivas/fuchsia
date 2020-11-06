// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <zircon/errors.h>

lr .req x30
dst .req x0
src .req x1
len .req x2
fault_return_ptr .req x3
fault_return_mask .req x4
temp .req x9

// NOTE! We know here that the memcpy code doesn't touch these registers,
// so we can use them to save values.  But they are call-clobbered in the
// C ABI, so we don't need a stack frame.
saved_fault_return_ptr .req x16
saved_lr .req x17

// Arm64UserCopyRet _arm64_user_copy(void *dst, const void *src, size_t len, uint64_t *fault_return, uint64_t fault_return_mask)
.section .text._arm64_user_copy,"ax"
.balign 64 // Align to cache line.  This code fits in one cache line.
FUNCTION(_arm64_user_copy)

    adr temp, .Lfault_from_user
    and temp, temp, fault_return_mask

    mov saved_fault_return_ptr, fault_return_ptr
    .cfi_register fault_return_ptr, saved_fault_return_ptr
    mov saved_lr, lr
    .cfi_register lr, saved_lr

    // Just call our normal memcpy.  The caller has ensured that the
    // address range is in the user portion of the address space.
    // While fault_return_ptr is set, userspace data faults will be
    // redirected to .Lfault_from_user, below.
    //
    // NOTE! We make important assumptions here about what the memcpy
    // code does: it never moves the stack pointer, and it never touches
    // the registers we're using for saved_fault_return_ptr and saved_lr.
    str temp, [fault_return_ptr]
    bl memcpy
    // Store a successful status for the return. In this case since we do not set x1 the value of
    // the fault address in the return struct is undefined.
    mov x0, #ZX_OK

.Luser_copy_return:
    str xzr, [saved_fault_return_ptr]
    mov lr, saved_lr
    .cfi_same_value lr
    ret
    SPECULATION_POSTFENCE
END_FUNCTION(_arm64_user_copy)

.section .text.cold._arm64_user_copy,"ax"
// If we are capturing faults the exception handler will have placed the faulting virtual address
// for us in x1 and the flags in x2. We do not touch x1 and rely on the caller to know if the value
// is meaningful based on whether it specified fault capture or not, we just need to construct a
// valid x0 before jmping to user_copy_return.
.Lfault_from_user:
    .cfi_startproc
    .cfi_register fault_return_ptr, saved_fault_return_ptr
    .cfi_register lr, saved_lr
    mov x0, #ZX_ERR_INVALID_ARGS
    // If we are capturing faults the flags will have been placed in x2 and we want them placed in
    // the high bits of x0. If not capturing faults then we will copy some garbage bits which will
    // be ignored by the caller.
    bfi x0, x2, 32, 32
    b .Luser_copy_return
    .cfi_endproc
