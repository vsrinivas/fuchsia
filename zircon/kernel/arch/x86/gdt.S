// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86/asm.h>
#include <arch/x86/descriptor.h>

.section .rodata.gdt,"a",%progbits
// Intentionlly misalign this by 6 so that the address word holding _temp_gdt
// is naturally aligned.  This address gets fixed up by apply_fixups (image.S).
.balign 8
.skip 6
DATA(_temp_gdtr)
    .short _temp_gdt_end - _temp_gdt - 1
    .quad _temp_gdt
END_DATA(_temp_gdtr)

.data
.align 8
DATA(_temp_gdt)
    /* null entry */
    .int 0
    .int 0

    /* CODE_SELECTOR */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00             /* base 23:16 */
    .byte  0b10011010       /* P(1) DPL(00) S(1) 1 C(0) R(1) A(0) */
    .byte  0b11001111       /* G(1) D(1) 0 0 limit 19:16 */
    .byte  0x0              /* base 31:24 */

    /* CODE_64_SELECTOR */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00             /* base 23:16 */
    .byte  0b10011010       /* P(1) DPL(00) S(1) 1 C(0) R(1) A(0) */
    .byte  0b10101111       /* G(1) D(0) L(1) AVL(0) limit 19:16 */
    .byte  0x0              /* base 31:24 */

    /* DATA_SELECTOR */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00             /* base 23:16 */
    .byte  0b10010010       /* P(1) DPL(00) S(1) 0 E(0) W(1) A(0) */
    .byte  0b11001111       /* G(1) B(1) 0 0 limit 19:16 */
    .byte  0x0              /* base 31:24 */

    /* disable 32bit ring3 code descriptor on 64bit kernel */
    .int  0x0
    .int  0x0

    /* USER_DATA_SELECTOR */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00             /* base 23:16 */
    .byte  0b11110010       /* P(1) DPL(11) S(1) 0 E(0) W(1) A(0) */
    .byte  0b11001111       /* G(1) B(1) 0 0 limit 19:16 */
    .byte  0x0              /* base 31:24 */

    /* USER_CODE_64_SELECTOR */
    .short 0xffff           /* limit 15:00 */
    .short 0x0000           /* base 15:00 */
    .byte  0x00             /* base 23:16 */
    .byte  0b11111010       /* P(1) DPL(11) S(1) 1 C(0) R(1) A(0) */
    .byte  0b10101111       /* G(1) D(0) L(1) AVL(0) limit 19:16 */
    .byte  0x0              /* base 31:24 */

.rept SMP_MAX_CPUS
    /* TSS_SELECTORs */
    .short 0                /* limit 15:00 */
    .short 0                /* base 15:00 */
    .byte  0                /* base 23:16 */
    .byte  0b10001001       /* P(1) DPL(00) 0 10 B(0) 1 */
    .byte  0b10000000       /* G(1) 0 0 AVL(0) limit 19:16 */
    .byte  0                /* base 31:24 */
    /* second half of 64bit desciptor */
    .int   0x00000000       /* base 63:32 */
    .int   0x00000000       /* reserved/sbz */
.endr

END_DATA(_temp_gdt)

DATA(_temp_gdt_end)
