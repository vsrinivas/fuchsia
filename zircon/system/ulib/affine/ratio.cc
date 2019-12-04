// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/affine/ratio.h>
#include <type_traits>
#include <zircon/assert.h>

namespace affine {
namespace {

// Calculates the greatest common denominator (factor) of two values.
template <typename T>
T BinaryGcd(T a, T b) {
  internal::DebugAssert(a && b);

  // Remove and count the common factors of 2.
  uint8_t twos;
  for (twos = 0; ((a | b) & 1) == 0; ++twos) {
    a >>= 1;
    b >>= 1;
  }

  // Get rid of the non-common factors of 2 in a. a is non-zero, so this
  // terminates.
  while ((a & 1) == 0) {
    a >>= 1;
  }

  do {
    // Get rid of the non-common factors of 2 in b. b is non-zero, so this
    // terminates.
    while ((b & 1) == 0) {
      b >>= 1;
    }

    // Apply the Euclid subtraction method.
    if (a > b) {
      std::swap(a, b);
    }

    b = b - a;
  } while (b != 0);

  // Multiply in the common factors of two.
  return a << twos;
}

enum class RoundDirection { Down, Up };

// Scales a uint64_t value by the ratio of two uint32_t values. If round_up is
// true, the result is rounded up rather than down. overflow is set to indicate
// overflow.
template <RoundDirection ROUND_DIR, uint64_t OVERFLOW_LIMIT_64>
uint64_t ScaleUInt64(uint64_t value, uint32_t numerator, uint32_t denominator) {
  constexpr uint64_t kLow32Bits = 0xffffffffu;

  // high and low are the product of the numerator and the high and low halves
  // (respectively) of value.
  uint64_t high = numerator * (value >> 32u);
  uint64_t low = numerator * (value & kLow32Bits);

  // Ignoring overflow and remainder, the result we want is:
  // ((high << 32) + low) / denominator.

  // Move the high end of low into the low end of high.
  high += low >> 32u;
  low = low & kLow32Bits;

  // Ignoring overflow and remainder, the result we want is still:
  // ((high << 32) + low) / denominator.

  // Compute the divmod of high/D
  uint64_t high_q = high / denominator;
  uint64_t high_r = high % denominator;

  // If high_q is larger than the overflow limit, then we can just get out now.
  // The overflow limit will be different depending on whether we are scaling
  // a non-negative number (0x7FFFFFFF) or a negative number (0x80000000)
  constexpr uint64_t OVERFLOW_LIMIT_32 = OVERFLOW_LIMIT_64 >> 32;
  if (high_q > OVERFLOW_LIMIT_32) {
    return OVERFLOW_LIMIT_64;
  }

  // The remainder of high/D are the high bits of low.  Or them in, and do the
  // divmod for the low portion
  low |= high_r << 32u;

  uint64_t low_q = low / denominator;
  __UNUSED uint64_t low_r = low % denominator;

  uint64_t result = (high_q << 32u) | low_q;

  if constexpr (ROUND_DIR == RoundDirection::Up) {
    if (result >= OVERFLOW_LIMIT_64) {
      return OVERFLOW_LIMIT_64;
    }
    if (low_r) {
      ++result;
    }
  }

  return result;
}

constexpr bool FitsIn32Bits(uint64_t numerator, uint64_t denominator) {
  return ((numerator <= std::numeric_limits<uint32_t>::max()) &&
          (denominator <= std::numeric_limits<uint32_t>::max()));
}

}  // namespace

template <typename T>
void Ratio::Reduce(T* numerator, T* denominator) {
  internal::DebugAssert(numerator != nullptr);
  internal::DebugAssert(denominator != nullptr);
  internal::Assert(*denominator != 0);

  if (*numerator == 0) {
    *denominator = 1;
    return;
  }

  T gcd = BinaryGcd(*numerator, *denominator);
  internal::DebugAssert(gcd != 0);

  if (gcd == 1) {
    return;
  }

  *numerator = *numerator / gcd;
  *denominator = *denominator / gcd;
}

void Ratio::Product(uint32_t a_numerator, uint32_t a_denominator, uint32_t b_numerator,
                    uint32_t b_denominator, uint32_t* product_numerator,
                    uint32_t* product_denominator, Exact exact) {
  internal::DebugAssert(product_numerator != nullptr);
  internal::DebugAssert(product_denominator != nullptr);

  uint64_t numerator = static_cast<uint64_t>(a_numerator) * b_numerator;
  uint64_t denominator = static_cast<uint64_t>(a_denominator) * b_denominator;

  Ratio::Reduce(&numerator, &denominator);

  if (!FitsIn32Bits(numerator, denominator)) {
    internal::Assert(exact == Exact::No);

    // Try to find the best approximation of the ratio that we can.  Our
    // approach is as follows.  Figure out the number of bits to the right
    // we need to shift the numerator and denominator, rounding up or down
    // in the process, such that the result can be reduced to fit into 32
    // bits.
    //
    // This approach tends to beat out a just-shift-until-it-fits approach,
    // as well as an always-shift-then-reduce approach, but _none_ of these
    // approaches always finds the best solution.
    //
    // TODO(johngro) : figure out if it is reasonable to actually compute
    // the best solution.  Alternatively, consider implementing a "just
    // shift until it fits" solution if the approximate results are good
    // enough.
    //
    for (uint32_t i = 1; i <= 32; ++i) {
      // Produce a version of the numerator and denominator which have
      // each been divided by 2^i, rounding up/down as appropriate
      // (instead of truncating).
      uint64_t rounded_numerator = (numerator + (static_cast<uint64_t>(1) << (i - 1))) >> i;
      uint64_t rounded_denominator = (denominator + (static_cast<uint64_t>(1) << (i - 1))) >> i;

      if (rounded_denominator == 0) {
        // Product is larger than we can represent. Return the largest value we
        // can represent.
        *product_numerator = std::numeric_limits<uint32_t>::max();
        *product_denominator = 1;
        return;
      }

      if (rounded_numerator == 0) {
        // Product is smaller than we can represent. Return 0.
        *product_numerator = 0;
        *product_denominator = 1;
        return;
      }

      Ratio::Reduce(&rounded_numerator, &rounded_denominator);
      if (FitsIn32Bits(rounded_numerator, rounded_denominator)) {
        *product_numerator = static_cast<uint32_t>(rounded_numerator);
        *product_denominator = static_cast<uint32_t>(rounded_denominator);
        return;
      }
    }
  }

  *product_numerator = static_cast<uint32_t>(numerator);
  *product_denominator = static_cast<uint32_t>(denominator);
}

int64_t Ratio::Scale(int64_t value, uint32_t numerator, uint32_t denominator) {
  internal::Assert(denominator != 0u);

  if (value >= 0) {
    // LIMIT == 0x7FFFFFFFFFFFFFFF
    constexpr uint64_t LIMIT = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    return static_cast<int64_t>(ScaleUInt64<RoundDirection::Down, LIMIT>(
        static_cast<uint64_t>(value), numerator, denominator));
  } else {
    // LIMIT == 0x8000000000000000
    //
    // Note:  We are attempting to pass the unsigned distance from zero into
    // our ScaleUInt64 function.  In the case of negative numbers, we pass
    // the twos compliment into the scale function, and then flip the sign
    // again on the way out.
    //
    // We are taking the advantage of the fact that the twos compliment of
    // MIN is itself for any signed integer type, and that casting this
    // value to an unsigned integer of the same size properly produces the
    // original value's distance from zero.  Clamping the limit to the
    // distance of MIN from zero means that saturated results will likewise
    // get properly flipped back to MIN during the return.
    //
    constexpr uint64_t LIMIT = static_cast<uint64_t>(std::numeric_limits<int64_t>::min());
    return -static_cast<int64_t>(ScaleUInt64<RoundDirection::Up, LIMIT>(
        static_cast<uint64_t>(-value), numerator, denominator));
  }
}

// Explicit instantiation of the two supported types of Ratio
template void Ratio::Reduce<uint32_t>(uint32_t*, uint32_t*);
template void Ratio::Reduce<uint64_t>(uint64_t*, uint64_t*);

}  // namespace affine
