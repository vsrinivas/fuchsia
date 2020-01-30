// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdint.h>

#include <type_traits>

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_POPCOUNT_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_POPCOUNT_H_

namespace ktl {
namespace internal {

// Base prototype
template <typename T, typename = void>
struct popcount_helper;

// Specialization for 32 bit integers.
template <typename T>
struct popcount_helper<T, typename std::enable_if<std::is_same<T, uint32_t>::value>::type> {
  static int popcount(uint32_t val) {
    // Implement a log2(bits) popcount, with a few tricks to save some
    // instructions.
    //
    // The general idea here is to simply add up the bits in the word in
    // parallel, storing intermediate results in different bit positions in
    // the word as we go, and eventually arriving at a sum which is the
    // popcount of the word.
    //
    // For example, consider an 8 bit integer written as
    //
    // b7 b6 b5 b4 b3 b2 b1 b0
    //
    // Each of the bits in the word represent a partial sum that, when added
    // together, produce our result.  We can compute...
    //
    // (
    // b7 b6 b5 b4 b3 b2 b1 b0  &
    //  0  1  0  1  0  1  0  1  =
    //  0 b6  0 b4  0 b2  0 b0
    // ) + (
    // b7 b6 b5 b4 b3 b2 b1 b0  >> 1 =
    //  0 b7 b6 b5 b4 b3 b2 b1  &
    //  0  1  0  1  0  1  0  1  =
    //  0 b7  0 b5  0 b3  0 b1
    // )                        =
    // b7+b6 b5+b4 b3+b2 b1+b0
    //
    // And now we have 4 partial sums, each of which takes two bits of
    // storage instead of one.  Repeating this process with appropriate
    // masks and shifts will eventually give us the answer we are looking
    // for.  After the next step, we will have partial sums which require 3
    // bits of storage, but aligned to 4 bit boundaries; so...
    //
    // 0 (b7+b6+b5+b4) 0 (b3+b2+b1+b0)
    //
    // After the last step, we will get the final sum stored in 4 bits, with
    // guaranteed 0s for the upper bits.
    // aligned to ...
    //
    // 0 0 0 0 (b7+b6+b5+b4+b3+b2+b1+b0)
    //
    // While this is the general idea, it turns out that we only need to
    // follow this exact process during step #2.  For each of the other
    // steps (there will be 5 for a 32 bit integer) we can shave a few
    // cycles by taking advantage of some of the particular properties of
    // each step.  See the comments below.
    constexpr uint32_t mask1 = 0x55555555;
    constexpr uint32_t mask2 = 0x33333333;
    constexpr uint32_t mask3 = 0x0F0F0F0F;
    constexpr uint32_t mult1 = 0x01010101;

    // Step 1:
    // While we could compute (val & mask) + ((val >> 1) & mask), we can
    // actually save one of the mask operations by subtracting instead of
    // adding.  Consider the following truth table...
    //
    //  In | >>1 |  &  |  -  |
    // X00 | XX0 | X00 | X00 |
    // X01 | XX0 | X00 | X01 |
    // X10 | XX1 | X01 | X01 |
    // X11 | XX1 | X01 | X10 |
    //
    val = val - ((val >> 1) & mask1);

    // Step 2:
    // This is simply the operation described in the overview, adding a
    // bunch of 2 bit partials to produce a 3 bit result, but which is
    // aligned on 4 bit boundaries (where the MSB of each 4 bit nibble is
    // guaranteed to be 0)
    val = (val & mask2) + ((val >> 2) & mask2);

    // Step 3:
    // Again, we can save a mask operation here.  This time, it is because
    // the we know that all bit positions ((i * 4) + 3) are guaranteed to be
    // zero.  We can simply shift by 4 and add the partial sums.  Any
    // overflow will go into the ((i * 4) + 3) position and not interfere
    // with any of the other partial sums.
    val = (val + (val >> 4)) & mask3;

    // Step 4 + 5:
    // Finally, we can combine steps 4 and 5 using a multiply.  We have 4
    // remaining partial sums, each of which is contained in 4 bits and
    // aligned to 8 bit boundaries (with 4 bits of zero in-between each
    // sum).  Now, we have enough space that if we could perform all 4 sums
    // at once, we know that the result would fit in (at most) 6 bits, which
    // easily fits in our 8 bits of space.
    //
    // At its heart, multiplying is the equivalent of repeated shifting, and
    // conditional adding.  IOW - if I multiply by 0101b, it is basically
    // the same as saying "shift X left by 0, then add that to shift X left
    // by 2" (because bits 0 and bits 2 are the only bits set).
    //
    // So, we have, 0x0A0B0C0D, and want to compute (A+B+C+D).  Multiplying
    // our register by 0x01010101 basically does this for us.  We are
    // summing our register shifted left by 0 (A), 8 (B), 16 (C) and 24 (D),
    // which puts A+B+C+D into the upper 8 bits of the register.  The lower
    // 24 bits are junk, but we know (because of the 0s which separate A-D)
    // that this junk is not going to overflow into our result.  All we need
    // to do is right shift by 24, and we are done.
    return (val * mult1) >> 24;
  }
};

// Specialization for 64 bit integers.
template <typename T>
struct popcount_helper<T, typename std::enable_if<std::is_same<T, uint64_t>::value>::type> {
  static int popcount(uint64_t val) {
    // See notes above for how this works.  The 64 bit version of this is
    // identical to the 32 bit version, the only difference is that during
    // the final stage, we use the multiply trick to combine steps 4, 5, and
    // 6 to sum all 8 4-bit partial sums at once, fitting the result into a
    // 7 bit value.
    constexpr uint64_t mask1 = 0x5555555555555555;
    constexpr uint64_t mask2 = 0x3333333333333333;
    constexpr uint64_t mask3 = 0x0F0F0F0F0F0F0F0F;
    constexpr uint64_t mult1 = 0x0101010101010101;

    val = val - ((val >> 1) & mask1);
    val = (val & mask2) + ((val >> 2) & mask2);
    val = (val + (val >> 4)) & mask3;
    return static_cast<int>((val * mult1) >> 56);
  }
};

}  // namespace internal

template <typename T>
int popcount(T val) {
  return internal::popcount_helper<T>::popcount(val);
}

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_POPCOUNT_H_
