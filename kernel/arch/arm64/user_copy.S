// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>

#define dst   x0
#define src   x1
#define len   x2
#define len16 x4
#define fault_return_ptr  x3
#define temp  x9
#define tempw w9
#define temp2 x10

# zx_status_t _arm64_copy_from_user(void *dst, const void *src, size_t len, void **fault_return)
.align 4
FUNCTION(_arm64_copy_from_user)
    # do we have any work to do?
    cbz     len, .Ldone_from_user

    # hint to the cpu what we're about to do
    prfm    pldl1strm, [src]
    prfm    pstl1keep, [dst]

    # Setup data fault return
    adr     temp, .Lfault_from_user
    str     temp, [fault_return_ptr]

    # is it < 16 bytes?
    cmp     len, #16
    b.lt    .Lcopy_byte_from_user

    # copy 16 bytes at a time
    lsr     len16, len, #4
    .p2align 4
.Lcopy_double_from_user:
    ldtr    temp, [src]
    ldtr    temp2, [src, #8]
    stp     temp, temp2, [dst], #16
    add     src, src, #16
    sub     len16, len16, #1
    cbnz    len16, .Lcopy_double_from_user

    # see if we have any left
    and     len, len, #15
    cbz     len, .Ldone_from_user

    # bytewise copy
.Lcopy_byte_from_user:
    ldtrb   tempw, [src]
    strb    tempw, [dst], #1
    add     src, src, #1
    sub     len, len, #1
    cbnz    len, .Lcopy_byte_from_user

.Ldone_from_user:
    mov     x0, #ZX_OK
.Lcleanup_from_user:
    # Reset data fault return
    str     xzr, [fault_return_ptr]
    ret

.Lfault_from_user:
    mov     x0, #ZX_ERR_INVALID_ARGS
    b       .Lcleanup_from_user
END_FUNCTION(_arm64_copy_from_user)

# zx_status_t _arm64_copy_to_user(void *dst, const void *src, size_t len, void **fault_return)
.align 4
FUNCTION(_arm64_copy_to_user)
    # do we have any work to do?
    cbz     len, .Ldone_to_user

    # hint to the cpu what we're about to do
    prfm    pldl1strm, [src]
    prfm    pstl1keep, [dst]

    # Setup data fault return
    adr     temp, .Lfault_to_user
    str     temp, [fault_return_ptr]

    # is it < 16 bytes?
    cmp     len, #16
    b.lt    .Lcopy_byte_to_user

    # copy 16 bytes at a time
    lsr     len16, len, #4
    .p2align 4
.Lcopy_double_to_user:
    ldp     temp, temp2, [src], #16
    sttr    temp, [dst]
    sttr    temp2, [dst, #8]
    add     dst, dst, #16
    sub     len16, len16, #1
    cbnz    len16, .Lcopy_double_to_user

    # see if we have any left
    and     len, len, #15
    cbz     len, .Ldone_to_user

    # bytewise copy
.Lcopy_byte_to_user:
    ldrb    tempw, [src], #1
    sttrb   tempw, [dst]
    add     dst, dst, #1
    sub     len, len, #1
    cbnz    len, .Lcopy_byte_to_user

.Ldone_to_user:
    mov     x0, #ZX_OK
.Lcleanup_to_user:
    # Reset data fault return
    str     xzr, [fault_return_ptr]
    ret

.Lfault_to_user:
    mov     x0, #ZX_ERR_INVALID_ARGS
    b       .Lcleanup_to_user
END_FUNCTION(_arm64_copy_to_user)

