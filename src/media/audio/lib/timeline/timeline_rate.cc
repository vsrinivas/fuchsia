// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "timeline_rate.h"

#include <zircon/assert.h>

#include <iostream>
#include <limits>
#include <utility>

namespace media {

namespace {

// iostream is only used when this is set to true
constexpr bool kDebugPrecisionLoss = false;

// Macro that expands to METHODS(T) for all possible types T.
#define INSTANTIATE_FOR_EXTENDED_TIMELINE_TYPES(METHODS) \
  METHODS(uint32_t)                                      \
  METHODS(uint64_t)                                      \
  METHODS(__uint128_t)

// Calculates the greatest common denominator (factor) of two values.
template <typename T>
T BinaryGcd(T a, T b) {
  if (a == 0) {
    return b;
  }

  if (b == 0) {
    return a;
  }

  // Remove and count the common factors of 2.
  uint8_t twos;
  for (twos = 0; ((a | b) & 1) == 0; ++twos) {
    a >>= 1;
    b >>= 1;
  }

  // Get rid of the non-common factors of 2 in a. a is non-zero, so this terminates.
  while ((a & 1) == 0) {
    a >>= 1;
  }

  do {
    // Get rid of the non-common factors of 2 in b. b is non-zero, so this terminates.
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

// Reduces the ratio of *numerator and *denominator.
template <typename T>
void ReduceRatio(T* numerator, T* denominator) {
  ZX_DEBUG_ASSERT(numerator != nullptr);
  ZX_DEBUG_ASSERT(denominator != nullptr);
  ZX_DEBUG_ASSERT(*denominator != 0);

  T gcd = BinaryGcd(*numerator, *denominator);

  if (gcd == 0) {
    *denominator = 1;
    return;
  }

  if (gcd == 1) {
    return;
  }

  *numerator = *numerator / gcd;
  *denominator = *denominator / gcd;
}

}  // namespace

// static
const TimelineRate TimelineRate::Zero = TimelineRate(0, 1);

// static
const TimelineRate TimelineRate::NsPerSecond = TimelineRate(1'000'000'000L, 1);

// static
// Reduces the specified rate.
void TimelineRate::Reduce(uint64_t* subject_delta, uint64_t* reference_delta) {
  ReduceRatio(subject_delta, reference_delta);
}

// static
// Multiplies two given rates. If exact, ASSERTs that the rate fits into a uint64-based ratio; else
// reduces precision by simple right-shift as needed, until the result fits into uint64_t:uint64_t.
void TimelineRate::Product(uint64_t a_subject_delta, uint64_t a_reference_delta,
                           uint64_t b_subject_delta, uint64_t b_reference_delta,
                           uint64_t* product_subject_delta, uint64_t* product_reference_delta,
                           bool exact) {
  ZX_DEBUG_ASSERT(a_reference_delta != 0);
  ZX_DEBUG_ASSERT(b_reference_delta != 0);
  ZX_DEBUG_ASSERT(product_subject_delta != nullptr);
  ZX_DEBUG_ASSERT(product_reference_delta != nullptr);

  auto subject_delta = static_cast<__uint128_t>(a_subject_delta) * b_subject_delta;
  auto reference_delta = static_cast<__uint128_t>(a_reference_delta) * b_reference_delta;

  ReduceRatio(&subject_delta, &reference_delta);

  if (subject_delta > std::numeric_limits<uint64_t>::max() ||
      reference_delta > std::numeric_limits<uint64_t>::max()) {
    ZX_ASSERT(!exact);

    auto __UNUSED bits_lost = 0u;  // only used when kDebugPrecisionLoss is set to true

    do {
      subject_delta >>= 1;
      reference_delta >>= 1;
      if constexpr (kDebugPrecisionLoss) {
        ++bits_lost;
      }
    } while (subject_delta > std::numeric_limits<uint64_t>::max() ||
             reference_delta > std::numeric_limits<uint64_t>::max());

    if (reference_delta == 0) {
      // Product is larger than we can represent. Return the largest value we can represent.
      *product_subject_delta = std::numeric_limits<uint64_t>::max();
      *product_reference_delta = 1;
      return;
    }

    if constexpr (kDebugPrecisionLoss) {
      if (bits_lost > 0) {
        std::cout << "*************************************************************" << std::endl;
        std::cout << "During TimelineRate::Product, bit-precision was reduced by " << bits_lost
                  << std::endl;
        std::cout << "*************************************************************" << std::endl;
      }
    }
  }

  *product_subject_delta = static_cast<uint64_t>(subject_delta);
  *product_reference_delta = static_cast<uint64_t>(reference_delta);
}

// static
// Scales a reference value by a given rate, returning kOverflow if the result exceeds an int64_t.
//
// Internally, our int128_t can accomodate all possible [int64 * uint64] values (and then some).
// INT64_MIN * UINT64_MAX == INT128MIN     + INT64_MIN                 : plenty of room to spare
// INT64_MAX * UINT64_MAX == UINT128_MAX   - (UINT64_MAX + INT64_MAX)  : even more extra space
//
// For consistency across positive and negative |value|, we round any fractional remainders toward
// INT64_MIN. If |value| is negative, standard truncation ("round toward zero") cuts the wrong
// direction (e.g. -1/2 == 0, we want -1), so round_down represents that adjustment, if needed.
int64_t TimelineRate::Scale(int64_t value, uint64_t subject_delta, uint64_t reference_delta) {
  ZX_ASSERT(reference_delta != 0ul);

  __int128_t product = static_cast<__int128_t>(value) * subject_delta;
  int round_down = (value < 0 && product % reference_delta != 0) ? -1 : 0;
  __int128_t result = product / reference_delta + round_down;

  if (result > std::numeric_limits<int64_t>::max() ||
      result < std::numeric_limits<int64_t>::min()) {
    return TimelineRate::kOverflow;
  }

  return result;
}

}  // namespace media
