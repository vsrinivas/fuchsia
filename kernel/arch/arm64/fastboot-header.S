// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


// Optional header included in start.S needed to make fastboot happy
_fastboot_start:
    // magic instruction that gives us UEFI "MZ" signature
    add x13, x18, #0x16
    b header_end

    .quad   KERNEL_LOAD_OFFSET
    .quad   __data_end - _start
    .quad   0

    .quad   0
    .quad   0
    .quad   0
    .byte   'A'
    .byte   'R'
    .byte   'M'
    .byte   0x64
    .long   pe_header - _start
    .align 3

pe_header:
    .ascii  "PE"
    .short  0

    // coff header
    .short  0xaa64
    .short  2       // number of sections
    .long   0
    .long   0
    .long   1
    .short  section_table - optional_header
    .short  0x206

optional_header:
    .short  0x20B
    .byte   0x2
    .byte   0x14
    .long   _end - header_end
    .long   0
    .long   0
    .long   0   // should be entry point
    .long   header_end - _start

    .quad   0
    .long   0x1000
    .long   0x200
    .short  0
    .short  0
    .short  0
    .short  0
    .short  0
    .short  0
    .long   0

    .long   _end - _start
    .long   header_end - _start
    .long   0
    .short  0xA
    .short  0
    .quad   0
    .quad   0
    .quad   0
    .quad   0
    .long   0
    .long   6
    .quad   0
    .quad   0
    .quad   0
    .quad   0
    .quad   0
    .quad   0

section_table:
    .ascii  ".reloc"
    .byte   0
    .byte   0
    .long   0
    .long   0
    .long   0
    .long   0
    .long   0
    .long   0
    .short  0
    .short  0
    .long   0x42100040

    .ascii  ".text"
    .byte   0
    .byte   0
    .byte   0
    .long   _end - header_end
    .long   header_end - _start
    .long   0
    .long   0
    .long   0
    .long   0
    .short  0
    .short  0
    .long   0xE0500020
    .align 12

header_end:
    // fall through to arm_reset
