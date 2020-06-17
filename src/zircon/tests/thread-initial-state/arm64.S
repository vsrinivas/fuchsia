// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

.macro check_simd reg1 reg2
    fmov x0, \reg1
    cbnz x0, .Lfail
    fmov x0, \reg2
    cbnz x0, .Lfail
.endm

# int thread_entry(void *arg)
FUNCTION(thread_entry)
    bmi .Lfail
    beq .Lfail
    bcs .Lfail
    bvs .Lfail

    cbnz x1, .Lfail
    cbnz x2, .Lfail
    cbnz x3, .Lfail
    cbnz x4, .Lfail
    cbnz x5, .Lfail
    cbnz x6, .Lfail
    cbnz x7, .Lfail
    cbnz x8, .Lfail
    cbnz x9, .Lfail
    cbnz x10, .Lfail
    cbnz x11, .Lfail
    cbnz x12, .Lfail
    cbnz x13, .Lfail
    cbnz x14, .Lfail
    cbnz x15, .Lfail
    cbnz x16, .Lfail
    cbnz x17, .Lfail
    cbnz x18, .Lfail
    cbnz x19, .Lfail
    cbnz x20, .Lfail
    cbnz x21, .Lfail
    cbnz x22, .Lfail
    cbnz x23, .Lfail
    cbnz x24, .Lfail
    cbnz x25, .Lfail
    cbnz x26, .Lfail
    cbnz x27, .Lfail
    cbnz x28, .Lfail
    cbnz x29, .Lfail
    cbnz x30, .Lfail

    # test thread arg
    ldr x1, =0x12345678
    lsl x1, x1, 32
    ldr x2, =0x90abcdef
    orr x1, x2, x1
    sub x0, x0, x1
    cbnz x0, .Lfail

    check_simd d0, v0.D[1]
    check_simd d1, v1.D[1]
    check_simd d2, v2.D[1]
    check_simd d3, v3.D[1]
    check_simd d4, v4.D[1]
    check_simd d5, v5.D[1]
    check_simd d6, v6.D[1]
    check_simd d7, v7.D[1]
    check_simd d8, v8.D[1]
    check_simd d9, v9.D[1]
    check_simd d10, v10.D[1]
    check_simd d11, v11.D[1]
    check_simd d12, v12.D[1]
    check_simd d13, v13.D[1]
    check_simd d14, v14.D[1]
    check_simd d15, v15.D[1]
    check_simd d16, v16.D[1]
    check_simd d17, v17.D[1]
    check_simd d18, v18.D[1]
    check_simd d19, v19.D[1]
    check_simd d20, v20.D[1]
    check_simd d21, v21.D[1]
    check_simd d22, v22.D[1]
    check_simd d23, v23.D[1]
    check_simd d24, v24.D[1]
    check_simd d25, v25.D[1]
    check_simd d26, v26.D[1]
    check_simd d27, v27.D[1]
    check_simd d28, v28.D[1]
    check_simd d29, v29.D[1]
    check_simd d30, v30.D[1]
    check_simd d31, v31.D[1]
    b zx_thread_exit
.Lfail:
    b print_fail
