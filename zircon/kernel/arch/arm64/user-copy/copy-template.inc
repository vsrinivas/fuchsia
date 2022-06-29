// Copyright 2022 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Template for implementing user copy.
//
// Based on cortex strings unrolling decisions for optimal memcopy as a starting point.
//
// Inputs:
//  * x0 destination address
//  * x1 source address
//  * x2 count in bytes
//
// Registers: x1-x13 are clobbered.
//
// Requires <arch/arm64/asm_macros.h> to be in the including context.
//
// Requires the following token to be defined (C Propocessor) as a prefix for labels.
//  * UC_PREFIX
//
// By default, since the original code did it, we align for the destination address(stores).
// This can be overriden by defining to a non zero value:
//  * UC_ALIGN_SRC
//
// Requires the following instruction macros to be defined:
//  * ld|st_byte register, base_address, offset=0
//  * ld|st_reg register, base_address, offset=0
//  * ld|st_pair register_0, register_1, offset=0, update_base_address= 0 | 1

#define UC_XCONCAT(prefix, name) prefix##name
#define UC_CONCAT(prefix, name) UC_XCONCAT(prefix, name)
#define UC_L(name) UC_CONCAT(.L, UC_CONCAT(UC_PREFIX, name))

// Immutable.
const_dst
    .req x0

        // Mutable.
        cnt
    .req x2

        // Pointers.
        src.req x1 dst
    .req x3

        // Pointers to end of region.
        srcend.req x4 dstend
    .req x5

        // Dedicated registers for ld/st data.
        a_0.req x6 a_1.req x7 a_2.req x8 a_3.req x9 a_4.req x10 a_5.req x11 a_6.req x12 a_7
    .req x13

        // Usable only when the original registers are being discarded.
        // Same as a_n, but denoted differently since they have an additional requirement.
        b_0.req src b_1.req cnt b_2.req dst b_3
    .req srcend

        // 32 bit aliases meant to deal with byte load/store instructions.
        c_0.req w6 c_1.req w7 c_2.req w8 c_3
    .req w9

        // Temps for intemerdiate values.
        // Do not use this aliases for ld/st instructions.
        tmp_1
    .req a_7

    .balign 64 UC_L(copy_start)
    : prfm PLDL1KEEP,
[src] mov dst, const_dst add dstend, const_dst, cnt add srcend, src, cnt cmp cnt,
#16 b.ls UC_L(copy_small_0_16)
cmp cnt, #96 b.hi UC_L(copy_loop_prologue)

// 17 - 64 byte unrolling.
sub tmp_1, cnt,
    1
    // There are at least 17 bytes, any path will have to load this.
    ld_pair a_0,
    a_1, src tbnz tmp_1, #6, UC_L(copy_medium_65_96) ld_pair a_4, a_5, srcend, -16 tbz tmp_1, #5,
    1f
    // Copies bytes in the middle [16, 48].
    ld_pair a_2,
    a_3, src, 16 ld_pair a_6, a_7, srcend, -32 st_pair a_2, a_3, dst, 16 st_pair a_6, a_7, dstend,
    -32

    // Copy first 16 and last 16 bytes.
    1 : st_pair a_0,
    a_1, dst st_pair a_4, a_5, dstend,
    -16 b UC_L(copy_exit)

         .balign 16 UC_L(copy_medium_65_96)
    :  // Load first 64 bytes from the beginning.
       // a_0 and a_1 have already been loaded.
      ld_pair a_2,
a_3, src, 16 ld_pair a_4, a_5, src, 32 ld_pair a_6, a_7, src, 48
    // Load last 32 bytes from the end.
    // This invalidates src, cnt.
    ld_pair b_0,
b_1, srcend, -32
             // This invalidates dst, srcend.
             ld_pair b_2,
b_3, srcend, -16
             // Write to the destination(const_dst, dstend) and the overlapping range
             // covers any alignment and length combination within that range.
             st_pair a_0,
a_1, const_dst st_pair a_2, a_3, const_dst, 16 st_pair a_4, a_5, const_dst,
32 st_pair a_6, a_7, const_dst, 48 st_pair b_0, b_1, dstend, -32 st_pair b_2, b_3, dstend,
-16 b UC_L(copy_exit)

    UC_L(copy_small_0_16)
    : cmp cnt,
#8 b.lo 2f

    // Case 1: cnt 8 ... 16
    // This works by reading, possibly overlapping, ranges of 8 bytes, and writing them back.
    // This is possible by reading the first 8 and the last 8.
    // cnt >= 8
    1 : ld_reg a_0,
    src ld_reg a_1, srcend, -8 st_reg a_0, dst, st_reg a_1, dstend,
    -8 b UC_L(copy_exit)

    // Case 2: cnt 4 ... 7
    2 : tbz cnt,
    #2, 3f ld_reg c_0, src ld_reg c_1, srcend, -4 st_reg c_0, dst, st_reg c_1, dstend,
    -4 b UC_L(copy_exit)

    // Case 3: cnt 0 ... 3
    // Limitations on certain ld/st variants, prevents us from using a register for the offset.
    // This has the side-effect of forcing us to deviate from the original approach that cortex
    // memcpy implementation has.
    //
    // To reduce the number of branches/jumps we unfold the last three byte copy into 3 separate
    // ones.
    3 : cbz cnt,
    UC_L(copy_exit) cmp cnt,
    #3 beq 4f
    // Copies 1 byte twice(cnt == 1) or copies two bytes(cnt == 2).
    ld_byte c_0,
    src ld_byte c_1, srcend, -1 st_byte c_0, dst st_byte c_1, dstend,
    -1 b UC_L(copy_exit)

        4 :  // 3 bytes
             ld_byte c_0,
    src ld_byte c_1, src, 1 ld_byte c_2, src, 2 st_byte c_0, dst st_byte c_1, dst,
    1 st_byte c_2, dst,
    2 b UC_L(copy_exit)

    // There are at least 97 bytes to load.
    UC_L(copy_loop_prologue)
    :  // Aligns src or dst for the loop body.
       // The number of aligned bytes is stored in tmp_1.
      and tmp_1,
      const_dst,
      #15

      // This aligns dst and points to -16 if aligned copy starts at 0.
      bic dst,
      const_dst,
      #15

      // Load possibly overlappng
      ld_pair a_4,
      a_5,
      src

      // Add to cnt the unaligned bytes, will be adjusted later.
      // cannot use |a_7| and |tmp_1|.
      add cnt,
      cnt,
      tmp_1

      // This points |src - unaligned| such that aligned copy starts at src + 16 - unaligned.
      sub src,
      src,
      tmp_1

      // Body of the loop expects a_0 ... a_7 to be loaded before hand.
      ld_pair a_0,
      a_1,
      src,
      16
      // Invalidates |tmp_1|.
      st_pair a_4,
      a_5,
      const_dst ld_pair a_2,
      a_3,
      src,
      32 ld_pair a_4,
      a_5,
      src,
      48 ld_pair a_6,
      a_7,
      src,
      64,
      1

      // If we have less than 128 bytes(+16 for the 'adjusted' cnt), then we can skip
      // into the loop epilogue.
      subs cnt,
      cnt,
      128 + 16 b.ls UC_L(copy_loop_epilogue)

            // store the loaded a_0...a_n registers into dst + 16 ... dst+ 64
            // Entering this loop a_0... a_7 should contain the data to be stored already.
            // Upon exiting this loop the initial contents of a_0 and a_7 have been stored,
            // and the contigous chunk has been loaded into memory.
            UC_L(copy_loop_body)
    : st_pair a_0,
      a_1,
      dst,
      16 ld_pair a_0,
      a_1,
      src,
      16 st_pair a_2,
      a_3,
      dst,
      32 ld_pair a_2,
      a_3,
      src,
      32 st_pair a_4,
      a_5,
      dst,
      48 ld_pair a_4,
      a_5,
      src,
      48 st_pair a_6,
      a_7,
      dst,
      64,
      1 ld_pair a_6,
      a_7,
      src,
      64,
      1 subs cnt,
      cnt,
      64 b.hi UC_L(copy_loop_body)

      // store the loaded a_0...a_n registers into dst + 16 ... dst+ 64 and
      // the following 64 bytes from the end. Any unaligned tail, ends up writing twice a
      // chunk of the last aligned store from the loop.
      UC_L(copy_loop_epilogue)
    :  // Invalidates src and cnt.
      ld_pair b_0,
      b_1,
      srcend,
      -64 st_pair a_0,
      a_1,
      dst,
      16 ld_pair a_0,
      a_1,
      srcend,
      -48 st_pair a_2,
      a_3,
      dst,
      32 ld_pair a_2,
      a_3,
      srcend,
      -32 st_pair a_4,
      a_5,
      dst,
      48 ld_pair a_4,
      a_5,
      srcend,
      -16 st_pair a_6,
      a_7,
      dst,
      64 st_pair b_0,
      b_1,
      dstend,
      -64 st_pair a_0,
      a_1,
      dstend,
      -48 st_pair a_2,
      a_3,
      dstend,
      -32 st_pair a_4,
      a_5,
      dstend,
      -16

      UC_L(copy_exit)
    :

// Clean up any locally defined macros.
#undef UC_CONCAT
#undef UC_XCONCAT
#undef UC_L
