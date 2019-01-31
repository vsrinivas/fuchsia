// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>

lr .req x30
dst .req x0
src .req x1
len .req x2
fault_return_ptr .req x3
temp .req x15

// NOTE! We know here that the memcpy code doesn't touch these registers,
// so we can use them to save values.  But they are call-clobbered in the
// C ABI, so we don't need a stack frame.
saved_fault_return_ptr .req x16
saved_lr .req x17

// zx_status_t _arm64_user_copy(void *dst, const void *src, size_t len, void **fault_return)
.section .text._arm64_user_copy,"ax"
.balign 64 // Align to cache line.  This code fits in one cache line.
FUNCTION(_arm64_user_copy)

    adr temp, .Lfault_from_user

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
    mov x0, #ZX_OK

.Luser_copy_return:
    str xzr, [saved_fault_return_ptr]
    mov lr, saved_lr
    .cfi_same_value lr
    ret
END_FUNCTION(_arm64_user_copy)

.section .text.cold._arm64_user_copy,"ax"
.Lfault_from_user:
    .cfi_startproc
    .cfi_register fault_return_ptr, saved_fault_return_ptr
    .cfi_register lr, saved_lr
    mov x0, #ZX_ERR_INVALID_ARGS
    b .Luser_copy_return
    .cfi_endproc
