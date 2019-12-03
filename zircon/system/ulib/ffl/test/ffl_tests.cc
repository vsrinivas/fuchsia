// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

#include <ffl/expression.h>
#include <ffl/fixed.h>
#include <ffl/saturating_arithmetic.h>
#include <zxtest/zxtest.h>

namespace {

using ffl::ComparisonTraits;
using ffl::Fixed;
using ffl::FormatIsValid;
using ffl::FromRatio;
using ffl::FromRaw;
using ffl::SaturateAdd;
using ffl::SaturateMultiply;
using ffl::SaturateSubtract;
using ffl::ToResolution;

// Helper for the macro below. Consumes the tokens passed as two boolean
// expressions. Returns |condition| when |enabled| is true, otherwise returns
// true when |enabled| is false.
static constexpr bool AssertCondition(bool condition, bool enabled) {
  return condition || !enabled;
}

// Asserts the given condition is true, unless the enablement is false.
#define static_assert_if(...) static_assert(AssertCondition(__VA_ARGS__))

// Assert that a 40bit shift (Q44.20 / Q44.20) compiles. This test verifies that
// integer constants defined in the conversion logic are properly typed for the
// required range when greater than 32bit.
static_assert(Fixed<int64_t, 20>{1} / Fixed<int64_t, 20>{FromRatio(1, 2)} == Fixed<int64_t, 20>{2});

// Test that the saturating arithmetic operations return the correct result when
// detecting overflow/underflow. Due to the extremely large set of combinations
// of operand and result size and signedness, this test is not exhaustive in the
// integral type space. Instead, it focuses on key signed/unsigned cases that
// exercise the sign comparison logic provided by this library and assumes that
// the overlfow detection provided by the compiler extends correctly to all
// other combinations of integral types.

template <typename T>
static constexpr T Min = std::numeric_limits<T>::min();

template <typename T>
static constexpr T Max = std::numeric_limits<T>::max();

template <typename T, typename U, typename R>
constexpr bool TestSaturatingArithmetic() {
  // Test signed values and result of the same size.
  if constexpr (std::is_signed_v<T> && std::is_signed_v<U> && std::is_signed_v<R> &&
                sizeof(T) == sizeof(R) && sizeof(U) == sizeof(R)) {
    static_assert(SaturateAdd<T, U, R>(Max<T>, +1) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, -1) == Max<R> - 1);
    static_assert(SaturateAdd<T, U, R>(Min<T>, +1) == Min<R> + 1);
    static_assert(SaturateAdd<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(+1, Max<U>) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Max<U>) == Max<R> - 1);
    static_assert(SaturateAdd<T, U, R>(+1, Min<U>) == Min<R> + 1);
    static_assert(SaturateAdd<T, U, R>(-1, Min<U>) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(Min<T>, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, Max<U>) == -1);
    static_assert(SaturateAdd<T, U, R>(Max<T>, Min<U>) == -1);
    static_assert(SaturateAdd<T, U, R>(Max<T>, Max<U>) == Max<R>);

    static_assert(SaturateSubtract<T, U, R>(Max<T>, +1) == Max<R> - 1);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, -1) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, -1) == Min<R> + 1);

    static_assert(SaturateSubtract<T, U, R>(+1, Max<U>) == Min<R> + 2);
    static_assert(SaturateSubtract<T, U, R>(-1, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(+1, Min<U>) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Min<U>) == Max<R>);

    static_assert(SaturateSubtract<T, U, R>(Min<T>, Min<U>) == 0);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Min<U>) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Max<U>) == 0);

    static_assert(SaturateMultiply<T, U, R>(Max<T>, +2) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, +2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, -2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, -2) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(+2, Max<U>) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(+2, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Min<U>) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(Min<T>, Min<U>) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Max<U>) == Max<R>);
  }

  // Test signed values and unsigned result of the same size.
  if constexpr (std::is_signed_v<T> && std::is_signed_v<U> && std::is_unsigned_v<R> &&
                sizeof(T) == sizeof(R) && sizeof(U) == sizeof(R)) {
    static_assert(SaturateAdd<T, U, R>(Max<T>, +1) == Max<R> / 2 + 1);
    static_assert(SaturateAdd<T, U, R>(Max<T>, -1) == Max<R> / 2 - 1);
    static_assert(SaturateAdd<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(+1, Max<U>) == Max<R> / 2 + 1);
    static_assert(SaturateAdd<T, U, R>(-1, Max<U>) == Max<R> / 2 - 1);
    static_assert(SaturateAdd<T, U, R>(+1, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Min<U>) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(Min<T>, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, Max<U>) == Max<R> - 1);

    static_assert(SaturateSubtract<T, U, R>(Max<T>, +1) == Max<R> / 2 - 1);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, -1) == Max<R> / 2 + 1);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateSubtract<T, U, R>(+1, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(+1, Min<U>) == Max<R> / 2 + 2);
    static_assert(SaturateSubtract<T, U, R>(-1, Min<U>) == Max<R> / 2);

    static_assert(SaturateSubtract<T, U, R>(Min<T>, Min<U>) == 0);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Min<U>) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Max<U>) == 0);

    static_assert(SaturateMultiply<T, U, R>(Max<T>, +2) == Max<R> - 1);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, +2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, -2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, -2) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(+2, Max<U>) == Max<R> - 1);
    static_assert(SaturateMultiply<T, U, R>(+2, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Min<U>) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(Min<T>, Min<U>) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Max<U>) == Max<R>);
  }

  // Test signed values and larger result.
  if constexpr (std::is_signed_v<T> && std::is_signed_v<U> && std::is_signed_v<R> &&
                sizeof(T) < sizeof(R) && sizeof(U) < sizeof(R)) {
    static_assert(SaturateAdd<T, U, R>(Max<T>, +1) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, -1) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, +1) > Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, -1) > Min<R>);

    static_assert(SaturateAdd<T, U, R>(+1, Max<U>) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Max<U>) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(+1, Min<U>) > Min<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Min<U>) > Min<R>);

    static_assert(SaturateAdd<T, U, R>(Min<T>, Min<U>) > Min<R>);
    static_assert_if(SaturateAdd<T, U, R>(Min<T>, Max<U>) == -1, sizeof(T) == sizeof(U));
    static_assert_if(SaturateAdd<T, U, R>(Max<T>, Min<U>) == -1, sizeof(T) == sizeof(U));
    static_assert(SaturateAdd<T, U, R>(Max<T>, Max<U>) < Max<R>);

    static_assert(SaturateSubtract<T, U, R>(Max<T>, +1) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, -1) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, +1) > Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, -1) > Min<R>);

    static_assert(SaturateSubtract<T, U, R>(+1, Max<U>) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Max<U>) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(+1, Min<U>) > Min<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Min<U>) > Min<R>);

    static_assert_if(SaturateSubtract<T, U, R>(Min<T>, Min<U>) == 0, sizeof(T) == sizeof(U));
    static_assert(SaturateSubtract<T, U, R>(Min<T>, Max<U>) > Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Min<U>) < Max<R>);
    static_assert_if(SaturateSubtract<T, U, R>(Max<T>, Max<U>) == 0, sizeof(T) == sizeof(U));

    static_assert(SaturateMultiply<T, U, R>(Max<T>, +2) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, +2) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, -2) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, -2) < Max<R>);

    static_assert(SaturateMultiply<T, U, R>(+2, Max<U>) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(+2, Min<U>) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Max<U>) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Min<U>) < Max<R>);

    static_assert(SaturateMultiply<T, U, R>(Min<T>, Min<U>) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, Max<U>) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Min<U>) > Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Max<U>) < Max<R>);
  }

  // Test signed values and larger unsigned result.
  if constexpr (std::is_signed_v<T> && std::is_signed_v<U> && std::is_unsigned_v<R> &&
                sizeof(T) < sizeof(R) && sizeof(U) < sizeof(R)) {
    static_assert(SaturateAdd<T, U, R>(Max<T>, +1) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, -1) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(+1, Max<U>) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Max<U>) < Max<R>);
    static_assert(SaturateAdd<T, U, R>(+1, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Min<U>) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(Min<T>, Min<U>) == Min<R>);
    static_assert_if(SaturateAdd<T, U, R>(Min<T>, Max<U>) == Min<R>, sizeof(T) >= sizeof(U));
    static_assert_if(SaturateAdd<T, U, R>(Max<T>, Min<U>) == Min<R>, sizeof(T) <= sizeof(U));
    static_assert(SaturateAdd<T, U, R>(Max<T>, Max<U>) < Max<R>);

    static_assert(SaturateSubtract<T, U, R>(Max<T>, +1) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, -1) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateSubtract<T, U, R>(+1, Max<U>) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Max<U>) < Max<R>);
    static_assert(SaturateSubtract<T, U, R>(+1, Min<U>) > Min<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Min<U>) > Min<R>);

    static_assert_if(SaturateSubtract<T, U, R>(Min<T>, Min<U>) == 0, sizeof(T) == sizeof(U));
    static_assert(SaturateSubtract<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Min<U>) < Max<R>);
    static_assert_if(SaturateSubtract<T, U, R>(Max<T>, Max<U>) == 0, sizeof(T) == sizeof(U));

    static_assert(SaturateMultiply<T, U, R>(Max<T>, +2) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, +2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, -2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, -2) < Max<R>);

    static_assert(SaturateMultiply<T, U, R>(+2, Max<U>) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(+2, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Min<U>) < Max<R>);

    static_assert(SaturateMultiply<T, U, R>(Min<T>, Min<U>) < Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Max<U>) < Max<R>);
  }

  // Test signed values and smaller result.
  if constexpr (std::is_signed_v<T> && std::is_signed_v<U> && std::is_signed_v<R> &&
                sizeof(T) > sizeof(R) && sizeof(U) > sizeof(R)) {
    static_assert(SaturateAdd<T, U, R>(Max<T>, +1) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(Max<T>, -1) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(+1, Max<U>) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Max<U>) == Max<R>);
    static_assert(SaturateAdd<T, U, R>(+1, Min<U>) == Min<R>);
    static_assert(SaturateAdd<T, U, R>(-1, Min<U>) == Min<R>);

    static_assert(SaturateAdd<T, U, R>(Min<T>, Min<U>) == Min<R>);
    static_assert_if(SaturateAdd<T, U, R>(Min<T>, Max<U>) == -1, sizeof(T) == sizeof(U));
    static_assert_if(SaturateAdd<T, U, R>(Max<T>, Min<U>) == -1, sizeof(T) == sizeof(U));
    static_assert(SaturateAdd<T, U, R>(Max<T>, Max<U>) == Max<R>);

    static_assert(SaturateSubtract<T, U, R>(Max<T>, +1) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, -1) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, +1) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Min<T>, -1) == Min<R>);

    static_assert(SaturateSubtract<T, U, R>(+1, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(+1, Min<U>) == Max<R>);
    static_assert(SaturateSubtract<T, U, R>(-1, Min<U>) == Max<R>);

    static_assert_if(SaturateSubtract<T, U, R>(Min<T>, Min<U>) == 0, sizeof(T) == sizeof(U));
    static_assert(SaturateSubtract<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateSubtract<T, U, R>(Max<T>, Min<U>) == Max<R>);
    static_assert_if(SaturateSubtract<T, U, R>(Max<T>, Max<U>) == 0, sizeof(T) == sizeof(U));

    static_assert(SaturateMultiply<T, U, R>(Max<T>, +2) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, +2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, -2) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, -2) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(+2, Max<U>) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(+2, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(-2, Min<U>) == Max<R>);

    static_assert(SaturateMultiply<T, U, R>(Min<T>, Min<U>) == Max<R>);
    static_assert(SaturateMultiply<T, U, R>(Min<T>, Max<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Min<U>) == Min<R>);
    static_assert(SaturateMultiply<T, U, R>(Max<T>, Max<U>) == Max<R>);
  }

  return true;
}

static_assert(TestSaturatingArithmetic<int8_t, int8_t, int8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, int16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, int32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, int64_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int8_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int8_t, int16_t, int8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, int16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, int32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, int64_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int16_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int8_t, int32_t, int8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, int16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, int32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, int64_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int32_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int8_t, int64_t, int8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, int16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, int32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, int64_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int8_t, int64_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int16_t, int8_t, int8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, int16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, int32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, int64_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int8_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int16_t, int16_t, int8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, int16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, int32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, int64_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int16_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int16_t, int32_t, int8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, int16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, int32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, int64_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int32_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int16_t, int64_t, int8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, int16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, int32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, int64_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int16_t, int64_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int32_t, int8_t, int8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, int16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, int32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, int64_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int8_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int32_t, int32_t, int8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, int16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, int32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, int64_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int32_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int32_t, int64_t, int8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, int16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, int32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, int64_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int32_t, int64_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int64_t, int8_t, int8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, int16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, int32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, int64_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int8_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int64_t, int16_t, int8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, int16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, int32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, int64_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int16_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int64_t, int32_t, int8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, int16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, int32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, int64_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int32_t, uint64_t>());

static_assert(TestSaturatingArithmetic<int64_t, int64_t, int8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, int16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, int32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, int64_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, uint8_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, uint16_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, uint32_t>());
static_assert(TestSaturatingArithmetic<int64_t, int64_t, uint64_t>());

template <typename Q>
constexpr Q Offset(Q base, typename Q::Format::Integer raw_offset) {
  using Integer = typename Q::Format::Integer;
  const Integer raw_value = static_cast<Integer>(base.raw_value() + raw_offset);
  return FromRaw<Q::Format::FractionalBits>(raw_value);
}

template <typename Q>
constexpr Q Raw(typename Q::Format::Integer raw_value) {
  return FromRaw<Q::Format::FractionalBits>(raw_value);
}

template <typename LeftHand, typename RightHand, typename Result, size_t FractionalBits>
constexpr bool TestSaturatingFixedPointArithmetic() {
  if constexpr (FormatIsValid<LeftHand, FractionalBits> &&
                FormatIsValid<RightHand, FractionalBits> && FormatIsValid<Result, FractionalBits>) {
    using T = Fixed<LeftHand, FractionalBits>;
    using U = Fixed<RightHand, FractionalBits>;
    using R = Fixed<Result, FractionalBits>;

    // Some comparisons are disabled when both arguments cannot represent the
    // value 1 precisely.
    constexpr bool ImpreciseOne = T::Format::ApproximateUnit && U::Format::ApproximateUnit;

    // Some comparisons are disabled when 64bit saturation changes the result,
    // relative to the result when using other sized integers.
    constexpr bool Truncating =
        T::Format::Bits == 64 && U::Format::Bits == 64 && R::Format::Bits == 64;

    if constexpr (T::Format::IsSigned && U::Format::IsSigned && R::Format::IsSigned &&
                  T::Format::Bits == R::Format::Bits && U::Format::Bits == R::Format::Bits) {
      static_assert(T::Max() + Raw<U>(+1) == R::Max());
      static_assert(T::Max() + Raw<U>(-1) == Offset(R::Max(), -1));
      static_assert(T::Min() + Raw<U>(+1) == Offset(R::Min(), +1));
      static_assert(T::Min() + Raw<U>(-1) == R::Min());

      static_assert(Raw<T>(+1) + U::Max() == R::Max());
      static_assert(Raw<T>(-1) + U::Max() == Offset(R::Max(), -1));
      static_assert(Raw<T>(+1) + U::Min() == Offset(R::Min(), +1));
      static_assert(Raw<T>(-1) + U::Min() == R::Min());

      static_assert(T::Min() + U::Min() == R::Min());
      static_assert(T::Min() + U::Max() == Offset(R{0}, -1));
      static_assert(T::Max() + U::Min() == Offset(R{0}, -1));
      static_assert(T::Max() + U::Max() == R::Max());

      static_assert(T::Max() - Raw<U>(+1) == Offset(R::Max(), -1));
      static_assert(T::Max() - Raw<U>(-1) == R::Max());
      static_assert(T::Min() - Raw<U>(+1) == R::Min());
      static_assert(T::Min() - Raw<U>(-1) == Offset(R::Min(), +1));

      static_assert(Raw<T>(+1) - U::Max() == Offset(R::Min(), +2));
      static_assert(Raw<T>(-1) - U::Max() == R::Min());
      static_assert(Raw<T>(+1) - U::Min() == R::Max());
      static_assert(Raw<T>(-1) - U::Min() == R::Max());

      static_assert(T::Min() - U::Min() == R{0});
      static_assert(T::Min() - U::Max() == R::Min());
      static_assert(T::Max() - U::Min() == R::Max());
      static_assert(T::Max() - U::Max() == R{0});

      if constexpr (T::Format::FractionalBits + U::Format::FractionalBits < 64 && !Truncating) {
        static_assert_if(T::Max() * U{+1} == R::Max(), !ImpreciseOne);
        static_assert_if(T::Min() * U{+1} == R::Min(), !ImpreciseOne);
        static_assert(T::Max() * U{-1} == Offset(R::Min(), +1));
        static_assert(T::Min() * U{-1} == R::Max());

        static_assert_if(T{+1} * U::Max() == R::Max(), !ImpreciseOne);
        static_assert_if(T{+1} * U::Min() == R::Min(), !ImpreciseOne);
        static_assert(T{-1} * U::Max() == Offset(R::Min(), +1));
        static_assert(T{-1} * U::Min() == R::Max());

        static_assert_if(T::Max() * U{+2} == R::Max(), U::Format::IntegralBits > 1);
        static_assert_if(T::Min() * U{+2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Max() * U{-2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Min() * U{-2} == R::Max(), U::Format::IntegralBits > 1);

        static_assert_if(T{+2} * U::Max() == R::Max(), T::Format::IntegralBits > 1);
        static_assert_if(T{+2} * U::Min() == R::Min(), T::Format::IntegralBits > 1);
        static_assert_if(T{-2} * U::Max() == R::Min(), T::Format::IntegralBits > 1);
        static_assert_if(T{-2} * U::Min() == R::Max(), T::Format::IntegralBits > 1);

        static_assert(T::Min() * U::Min() == R::Max());
        static_assert_if(T::Min() * U::Max() == R::Min(), !ImpreciseOne);
        static_assert_if(T::Max() * U::Min() == R::Min(), !ImpreciseOne);
        static_assert_if(T::Max() * U::Max() == R::Max(), !ImpreciseOne);
      }
    }

    if constexpr (T::Format::IsSigned && U::Format::IsSigned && R::Format::IsUnsigned &&
                  T::Format::Bits == R::Format::Bits && U::Format::Bits == R::Format::Bits) {
      static_assert_if(T::Max() + Raw<U>(+1) == Offset<R>(R::Max() / 2, +1), !Truncating);
      static_assert_if(T::Max() + Raw<U>(+1) == R{T::Max()}, Truncating);
      static_assert(T::Max() + Raw<U>(-1) == Offset<R>(R::Max() / 2, -1));
      static_assert(T::Min() + Raw<U>(+1) == R::Min());
      static_assert(T::Min() + Raw<U>(-1) == R::Min());

      static_assert_if(Raw<T>(+1) + U::Max() == Offset<R>(R::Max() / 2, +1), !Truncating);
      static_assert_if(Raw<T>(+1) + U::Max() == R{U::Max()}, Truncating);
      static_assert(Raw<T>(-1) + U::Max() == Offset<R>(R::Max() / 2, -1));
      static_assert(Raw<T>(+1) + U::Min() == R::Min());
      static_assert(Raw<T>(-1) + U::Min() == R::Min());

      static_assert(T::Min() + U::Min() == R::Min());
      static_assert(T::Min() + U::Max() == R::Min());
      static_assert(T::Max() + U::Min() == R::Min());
      static_assert_if(T::Max() + U::Max() == Offset(R::Max(), -1), !Truncating);
      static_assert_if(T::Max() + U::Max() == R{T::Max()}, Truncating);

      static_assert(T::Max() - Raw<U>(+1) == Offset<R>(R::Max() / 2, -1));
      static_assert_if(T::Max() - Raw<U>(-1) == Offset<R>(R::Max() / 2, +1), !Truncating);
      static_assert_if(T::Max() - Raw<U>(-1) == R{T::Max()}, Truncating);
      static_assert(T::Min() - Raw<U>(+1) == R::Min());
      static_assert(T::Min() - Raw<U>(-1) == R::Min());

      static_assert(Raw<T>(+1) - U::Max() == R::Min());
      static_assert(Raw<T>(-1) - U::Max() == R::Min());
      static_assert_if(Raw<T>(+1) - U::Min() == Offset<R>(R::Max() / 2, +2), !Truncating);
      static_assert_if(Raw<T>(+1) - U::Min() == R{T::Max()}, Truncating);
      static_assert(Raw<T>(-1) - U::Min() == R{R::Max() / 2});

      static_assert(T::Min() - U::Min() == R{0});
      static_assert(T::Min() - U::Max() == R::Min());
      static_assert_if(T::Max() - U::Min() == R::Max(), !Truncating);
      static_assert_if(T::Max() - U::Min() == R{T::Max()}, Truncating);
      static_assert(T::Max() - U::Max() == R{0});

      if constexpr (T::Format::FractionalBits + U::Format::FractionalBits < 64) {
        static_assert_if(T::Max() * U{+1} == R{R::Max() / 2}, !ImpreciseOne && !Truncating);
        static_assert_if(T::Min() * U{+1} == R::Min(), !ImpreciseOne);
        static_assert(T::Max() * U{-1} == R::Min());
        static_assert_if(T::Min() * U{-1} == Offset<R>(R::Max() / 2, +1), !Truncating);

        static_assert_if(T{+1} * U::Max() == R{R::Max() / 2}, !ImpreciseOne && !Truncating);
        static_assert_if(T{+1} * U::Min() == R::Min(), !ImpreciseOne);
        static_assert(T{-1} * U::Max() == R::Min());
        static_assert_if(T{-1} * U::Min() == Offset<R>(R::Max() / 2, +1), !Truncating);

        static_assert_if(T::Max() * U{+2} == Offset<R>(R::Max(), -1),
                         U::Format::IntegralBits > 2 && !Truncating);
        static_assert_if(T::Max() * U{+2} == Offset<R>(R::Max(), -3), U::Format::IntegralBits == 1);
        static_assert_if(T::Min() * U{+2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Max() * U{-2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Min() * U{-2} == R::Max(), U::Format::IntegralBits > 1 && !Truncating);

        static_assert_if(T{+2} * U::Max() == Offset<R>(R::Max(), -1),
                         T::Format::IntegralBits > 2 && !Truncating);
        static_assert_if(T{+2} * U::Max() == Offset<R>(R::Max(), -3), T::Format::IntegralBits == 1);
        static_assert_if(T{+2} * U::Min() == R::Min(), T::Format::IntegralBits > 1);
        static_assert_if(T{-2} * U::Max() == R::Min(), T::Format::IntegralBits > 1);
        static_assert_if(T{-2} * U::Min() == R::Max(), T::Format::IntegralBits > 1 && !Truncating);

        static_assert_if(T::Min() * U::Min() == R::Max(),
                         U::Format::IntegralBits > 1 && !Truncating);
        static_assert_if(T::Min() * U::Max() == R::Min(), !ImpreciseOne);
        static_assert_if(T::Max() * U::Min() == R::Min(), !ImpreciseOne);
        static_assert_if(T::Max() * U::Max() == R::Max(),
                         !ImpreciseOne && U::Format::IntegralBits > 2 && !Truncating);
        static_assert_if(T::Max() * U::Max() == Offset<R>(R::Max(), -3),
                         !ImpreciseOne && U::Format::IntegralBits == 1);
      }
    }

    if constexpr (T::Format::IsSigned && U::Format::IsSigned && R::Format::IsSigned &&
                  T::Format::Bits < R::Format::Bits && U::Format::Bits < R::Format::Bits) {
      static_assert(T::Max() + Raw<U>(+1) < R::Max());
      static_assert(T::Max() + Raw<U>(-1) < R::Max());
      static_assert(T::Min() + Raw<U>(+1) > R::Min());
      static_assert(T::Min() + Raw<U>(-1) > R::Min());

      static_assert(Raw<T>(+1) + U::Max() < R::Max());
      static_assert(Raw<T>(-1) + U::Max() < R::Max());
      static_assert(Raw<T>(+1) + U::Min() > R::Min());
      static_assert(Raw<T>(-1) + U::Min() > R::Min());

      static_assert(T::Min() + U::Min() > R::Min());
      static_assert_if(T::Min() + U::Max() == Offset(R{0}, -1), T::Format::Bits == U::Format::Bits);
      static_assert_if(T::Max() + U::Min() == Offset(R{0}, -1), T::Format::Bits == U::Format::Bits);
      static_assert(T::Max() + U::Max() < R::Max());

      static_assert(T::Max() - Raw<U>(+1) < R::Max());
      static_assert(T::Max() - Raw<U>(-1) < R::Max());
      static_assert(T::Min() - Raw<U>(+1) > R::Min());
      static_assert(T::Min() - Raw<U>(-1) > R::Min());

      static_assert(Raw<T>(+1) - U::Max() < R::Max());
      static_assert(Raw<T>(-1) - U::Max() < R::Max());
      static_assert(Raw<T>(+1) - U::Min() > R::Min());
      static_assert(Raw<T>(-1) - U::Min() > R::Min());

      static_assert_if(T::Min() - U::Min() == R{0}, T::Format::Bits == U::Format::Bits);
      static_assert(T::Min() - U::Max() > R::Min());
      static_assert(T::Max() - U::Min() < R::Max());
      static_assert_if(T::Max() - U::Max() == R{0}, T::Format::Bits == U::Format::Bits);

      static_assert(T::Max() * U{+1} < R::Max());
      static_assert(T::Min() * U{+1} > R::Min());
      static_assert(T::Max() * U{-1} > R::Min());
      static_assert(T::Min() * U{-1} < R::Max());

      static_assert(T{+1} * U::Max() < R::Max());
      static_assert(T{+1} * U::Min() > R::Min());
      static_assert(T{-1} * U::Max() > R::Min());
      static_assert(T{-1} * U::Min() < R::Max());

      static_assert(T::Max() * U{+2} < R::Max());
      static_assert(T::Min() * U{+2} > R::Min());
      static_assert(T::Max() * U{-2} > R::Min());
      static_assert(T::Min() * U{-2} < R::Max());

      static_assert(T{+2} * U::Max() < R::Max());
      static_assert(T{+2} * U::Min() > R::Min());
      static_assert(T{-2} * U::Max() > R::Min());
      static_assert(T{-2} * U::Min() < R::Max());

      static_assert(T::Min() * U::Min() < R::Max());
      static_assert(T::Min() * U::Max() > R::Min());
      static_assert(T::Max() * U::Min() > R::Min());
      static_assert(T::Max() * U::Max() < R::Max());
    }
  }

  return true;
}

template <typename LeftHand, typename RightHand, typename Result>
static constexpr bool TestSaturatingFixedPointArithmeticVaryBits() {
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 0>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 1>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 2>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 3>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 4>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 5>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 6>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 7>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 8>());

  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 13>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 14>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 15>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 16>());

  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 29>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 30>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 31>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 32>());

  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 61>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 62>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 63>());
  static_assert(TestSaturatingFixedPointArithmetic<LeftHand, RightHand, Result, 64>());

  return true;
}

template <typename LeftHand, typename RightHand>
static constexpr bool TestSaturatingFixedPointArithmeticVaryResult() {
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, int8_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, int16_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, int32_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, int64_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, uint8_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, uint16_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, uint32_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryBits<LeftHand, RightHand, uint64_t>());

  return true;
}

template <typename LeftHand>
static constexpr bool TestSaturatingFixedPointArithmeticVaryRightHand() {
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, int8_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, int16_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, int32_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, int64_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, uint8_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, uint16_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, uint32_t>());
  static_assert(TestSaturatingFixedPointArithmeticVaryResult<LeftHand, uint64_t>());

  return true;
}

static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<int8_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<int16_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<int32_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<int64_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<uint8_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<uint16_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<uint32_t>());
static_assert(TestSaturatingFixedPointArithmeticVaryRightHand<uint64_t>());

// Fixed-to-fixed comparisons promote to the least resolution and greatest
// precision.
static_assert(Fixed<int, 0>{1} > Fixed<int, 1>::FromRaw(0));
static_assert(Fixed<int, 0>{1} > Fixed<int, 1>::FromRaw(1));
static_assert(Fixed<int, 0>{1} > Fixed<int, 2>::FromRaw(1));
static_assert(Fixed<int, 0>{1} > Fixed<int, 2>::FromRaw(2));
static_assert(Fixed<int, 0>{1} == Fixed<int, 2>::FromRaw(3));  // Round half to even.
static_assert(Fixed<int, 0>{1} == Fixed<int, 2>::FromRaw(4));  // Round half to even.
static_assert(Fixed<int, 0>{1} == Fixed<int, 2>::FromRaw(5));  // Round half to even.

static_assert(Fixed<int, 0>{1} >= Fixed<int, 1>::FromRaw(0));
static_assert(Fixed<int, 0>{1} >= Fixed<int, 1>::FromRaw(1));
static_assert(Fixed<int, 0>{1} >= Fixed<int, 2>::FromRaw(1));
static_assert(Fixed<int, 0>{1} >= Fixed<int, 2>::FromRaw(2));
static_assert(Fixed<int, 0>{1} >= Fixed<int, 2>::FromRaw(3));  // Round half to even.
static_assert(Fixed<int, 0>{1} >= Fixed<int, 2>::FromRaw(4));  // Round half to even.
static_assert(Fixed<int, 0>{1} >= Fixed<int, 2>::FromRaw(5));  // Round half to even.

static_assert(Fixed<int, 1>::FromRaw(0) < Fixed<int, 0>{1});
static_assert(Fixed<int, 1>::FromRaw(1) < Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(1) < Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(2) < Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(3) == Fixed<int, 0>{1});  // Round half to even.
static_assert(Fixed<int, 2>::FromRaw(4) == Fixed<int, 0>{1});  // Round half to even.
static_assert(Fixed<int, 2>::FromRaw(5) == Fixed<int, 0>{1});  // Round half to even.

static_assert(Fixed<int, 1>::FromRaw(0) <= Fixed<int, 0>{1});
static_assert(Fixed<int, 1>::FromRaw(1) <= Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(1) <= Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(2) <= Fixed<int, 0>{1});
static_assert(Fixed<int, 2>::FromRaw(3) <= Fixed<int, 0>{1});  // Round half to even.
static_assert(Fixed<int, 2>::FromRaw(4) <= Fixed<int, 0>{1});  // Round half to even.
static_assert(Fixed<int, 2>::FromRaw(5) <= Fixed<int, 0>{1});  // Round half to even.

#if 0 || TEST_DOES_NOT_COMPILE
static_assert(Fixed<int, 2>{1} == Fixed<unsigned, 2>{1});
static_assert(Fixed<unsigned, 2>{1} == Fixed<int, 2>{1});
#endif

// Test explicit conversion to like signs.
static_assert(Fixed<int, 2>{Fixed<unsigned, 2>{1}} == Fixed<int, 2>{1});
static_assert(Fixed<int, 2>{1} == Fixed<int, 2>{Fixed<unsigned, 2>{1}});

// Fixed-to-integer comparisons promote to the fixed-point resolution and the
// greatest precision.
static_assert(0 == Fixed<int, 1>::FromRaw(0));
static_assert(0 < Fixed<int, 1>::FromRaw(1));
static_assert(0 <= Fixed<int, 1>::FromRaw(1));
static_assert(0 <= Fixed<int, 1>::FromRaw(2));

static_assert(Fixed<int, 1>::FromRaw(0) == 0);
static_assert(Fixed<int, 1>::FromRaw(1) > 0);
static_assert(Fixed<int, 1>::FromRaw(1) >= 0);
static_assert(Fixed<int, 1>::FromRaw(2) >= 0);

static_assert(0 == Fixed<int, 2>::FromRaw(0));
static_assert(0 < Fixed<int, 2>::FromRaw(1));
static_assert(0 <= Fixed<int, 2>::FromRaw(1));
static_assert(0 < Fixed<int, 2>::FromRaw(2));
static_assert(0 <= Fixed<int, 2>::FromRaw(2));

static_assert(Fixed<int, 2>::FromRaw(0) == 0);
static_assert(Fixed<int, 2>::FromRaw(1) > 0);
static_assert(Fixed<int, 2>::FromRaw(1) >= 0);
static_assert(Fixed<int, 2>::FromRaw(2) > 0);
static_assert(Fixed<int, 2>::FromRaw(2) >= 0);

// Tests the fixed-to-fixed point conversion logic.
template <typename LeftInteger, size_t LeftFractionalBits, typename RightInteger,
          size_t RightFractionalBits>
static constexpr bool FixedComparisonPromotionTest() {
  if constexpr (FormatIsValid<LeftInteger, LeftFractionalBits> &&
                FormatIsValid<RightInteger, RightFractionalBits>) {
    using T = Fixed<LeftInteger, LeftFractionalBits>;
    using U = Fixed<RightInteger, RightFractionalBits>;
    using Comparison = ComparisonTraits<T, U>;

    static_assert((std::is_signed_v<LeftInteger> == std::is_signed_v<RightInteger>) ==
                  Comparison::value);

    // Fixed-to-fixed comparisons are only permitted on like signs.
    if constexpr (Comparison::value) {
      constexpr bool ImpreciseOne = T::Format::ApproximateUnit || U::Format::ApproximateUnit;

      constexpr size_t kGreatestRange = std::max(T::Format::IntegralBits, U::Format::IntegralBits);
      constexpr size_t kLeastResolution = std::min(LeftFractionalBits, RightFractionalBits);

      using Left = decltype(Comparison::Left(std::declval<T>()));
      using Right = decltype(Comparison::Right(std::declval<U>()));

      static_assert(std::is_same_v<Left, Right>);
      static_assert(Left::Format::FractionalBits == kLeastResolution);
      static_assert(Left::Format::IntegralBits >= kGreatestRange || Left::Format::Bits == 64);

      static_assert(T::Max() >= U::Min());
      static_assert(T::Min() <= U::Max());
      static_assert(T::Max() > U::Min());
      static_assert(T::Min() < U::Max());
      static_assert(T::Max() != U::Min());
      static_assert(T::Min() != U::Max());

      static_assert(T{0} == U{0});
      static_assert(T{0} >= U{0});
      static_assert(T{0} <= U{0});

      static_assert(T{1} != U{0});
      static_assert(T{1} >= U{0});
      static_assert(T{1} > U{0});

      static_assert(T{0} != U{1});
      static_assert(T{0} <= U{1});
      static_assert(T{0} < U{1});

      static_assert_if(T{1} == U{1}, !ImpreciseOne);
      static_assert_if(T{1} >= U{1}, !ImpreciseOne);
      static_assert_if(T{1} <= U{1}, !ImpreciseOne);

      if constexpr (T::Format::IsSigned && U::Format::IsSigned) {
        static_assert(T{-1} != U{0});
        static_assert(T{-1} < U{0});
        static_assert(T{-1} <= U{0});

        static_assert(T{0} != U{-1});
        static_assert(T{0} > U{-1});
        static_assert(T{0} >= U{-1});

        static_assert(T{-1} == U{-1});
        static_assert(T{-1} >= U{-1});
        static_assert(T{-1} <= U{-1});
      }
    }
  }

  return true;
}

template <typename LeftInteger, size_t LeftFractionalBits, typename RightInteger>
static constexpr bool FixedComparisonPromotionTestVaryRightFractionalBits() {
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 0>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 1>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 2>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 3>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 4>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 5>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 6>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 7>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 8>());

  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 13>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 14>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 15>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 16>());

  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 29>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 30>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 31>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 32>());

  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 61>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 62>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 63>());
  static_assert(FixedComparisonPromotionTest<LeftInteger, LeftFractionalBits, RightInteger, 64>());

  return true;
}

template <typename LeftInteger, size_t Bits>
static constexpr bool FixedComparisonPromotionTestVaryRightInteger() {
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, int8_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, int16_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, int32_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, int64_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, uint8_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, uint16_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, uint32_t>());
  static_assert(FixedComparisonPromotionTestVaryRightFractionalBits<LeftInteger, Bits, uint64_t>());

  return true;
}

template <typename LeftInteger>
static constexpr bool FixedComparisonPromotionTestVaryLeftFractionalBits() {
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 0>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 1>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 2>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 4>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 5>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 6>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 7>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 8>());

  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 13>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 14>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 15>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 16>());

  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 29>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 30>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 31>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 32>());

  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 61>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 62>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 63>());
  static_assert(FixedComparisonPromotionTestVaryRightInteger<LeftInteger, 64>());

  return true;
}

static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<int8_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<int16_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<int32_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<int64_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<uint8_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<uint16_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<uint32_t>());
static_assert(FixedComparisonPromotionTestVaryLeftFractionalBits<uint64_t>());

static_assert(1 == Fixed<int, 0>{1}.Ceiling());
static_assert(1 == Fixed<int, 1>{FromRatio(1, 2)}.Ceiling());
static_assert(1 == Fixed<int, 2>{FromRatio(1, 2)}.Ceiling());
static_assert(1 == Fixed<int, 2>{FromRatio(1, 4)}.Ceiling());
static_assert(0 == Fixed<int, 1>{FromRatio(-1, 2)}.Ceiling());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 2)}.Ceiling());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 4)}.Ceiling());
static_assert(-1 == Fixed<int, 0>{-1}.Ceiling());

static_assert(1 == Fixed<int8_t, 7>::Max().Ceiling());
static_assert(1 == Fixed<int16_t, 15>::Max().Ceiling());
static_assert(1 == Fixed<int32_t, 31>::Max().Ceiling());
static_assert(1 == Fixed<int64_t, 63>::Max().Ceiling());
static_assert(1 == Fixed<uint8_t, 8>::Max().Ceiling());
static_assert(1 == Fixed<uint16_t, 16>::Max().Ceiling());
static_assert(1 == Fixed<uint32_t, 32>::Max().Ceiling());
static_assert(1 == Fixed<uint64_t, 64>::Max().Ceiling());

static_assert(0 == Fixed<int8_t, 7>::Min().Ceiling());
static_assert(0 == Fixed<int16_t, 15>::Min().Ceiling());
static_assert(0 == Fixed<int32_t, 31>::Min().Ceiling());
static_assert(0 == Fixed<int64_t, 63>::Min().Ceiling());
static_assert(0 == Fixed<uint8_t, 8>::Min().Ceiling());
static_assert(0 == Fixed<uint16_t, 16>::Min().Ceiling());
static_assert(0 == Fixed<uint32_t, 32>::Min().Ceiling());
static_assert(0 == Fixed<uint64_t, 64>::Min().Ceiling());

static_assert(1 == Fixed<int, 0>{1}.Floor());
static_assert(0 == Fixed<int, 1>{FromRatio(1, 2)}.Floor());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 2)}.Floor());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 4)}.Floor());
static_assert(-1 == Fixed<int, 1>{FromRatio(-1, 2)}.Floor());
static_assert(-1 == Fixed<int, 2>{FromRatio(-1, 2)}.Floor());
static_assert(-1 == Fixed<int, 2>{FromRatio(-1, 4)}.Floor());
static_assert(-1 == Fixed<int, 0>{-1}.Floor());

static_assert(0 == Fixed<int8_t, 7>::Max().Floor());
static_assert(0 == Fixed<int16_t, 15>::Max().Floor());
static_assert(0 == Fixed<int32_t, 31>::Max().Floor());
static_assert(0 == Fixed<int64_t, 63>::Max().Floor());
static_assert(0 == Fixed<uint8_t, 8>::Max().Floor());
static_assert(0 == Fixed<uint16_t, 16>::Max().Floor());
static_assert(0 == Fixed<uint32_t, 32>::Max().Floor());
static_assert(0 == Fixed<uint64_t, 64>::Max().Floor());

static_assert(-1 == Fixed<int8_t, 7>::Min().Floor());
static_assert(-1 == Fixed<int16_t, 15>::Min().Floor());
static_assert(-1 == Fixed<int32_t, 31>::Min().Floor());
static_assert(-1 == Fixed<int64_t, 63>::Min().Floor());
static_assert(0 == Fixed<uint8_t, 8>::Min().Floor());
static_assert(0 == Fixed<uint16_t, 16>::Min().Floor());
static_assert(0 == Fixed<uint32_t, 32>::Min().Floor());
static_assert(0 == Fixed<uint64_t, 64>::Min().Floor());

static_assert(1 == Fixed<int, 0>{1}.Round());
static_assert(0 == Fixed<int, 1>{FromRatio(1, 2)}.Round());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 2)}.Round());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 4)}.Round());
static_assert(0 == Fixed<int, 1>{FromRatio(-1, 2)}.Round());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 2)}.Round());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 4)}.Round());
static_assert(-1 == Fixed<int, 0>{-1}.Round());

static_assert(1 == Fixed<int8_t, 7>::Max().Round());
static_assert(1 == Fixed<int16_t, 15>::Max().Round());
static_assert(1 == Fixed<int32_t, 31>::Max().Round());
static_assert(1 == Fixed<int64_t, 63>::Max().Round());
static_assert(1 == Fixed<uint8_t, 8>::Max().Round());
static_assert(1 == Fixed<uint16_t, 16>::Max().Round());
static_assert(1 == Fixed<uint32_t, 32>::Max().Round());
static_assert(1 == Fixed<uint64_t, 64>::Max().Round());

static_assert(-1 == Fixed<int8_t, 7>::Min().Round());
static_assert(-1 == Fixed<int16_t, 15>::Min().Round());
static_assert(-1 == Fixed<int32_t, 31>::Min().Round());
static_assert(-1 == Fixed<int64_t, 63>::Min().Round());
static_assert(0 == Fixed<uint8_t, 8>::Min().Round());
static_assert(0 == Fixed<uint16_t, 16>::Min().Round());
static_assert(0 == Fixed<uint32_t, 32>::Min().Round());
static_assert(0 == Fixed<uint64_t, 64>::Min().Round());

static_assert(Fixed<int, 2>{FromRatio(1, 1)} == Fixed<int, 2>{FromRatio(1, 1)}.Absolute());
static_assert(Fixed<int, 2>{FromRatio(1, 2)} == Fixed<int, 2>{FromRatio(1, 2)}.Absolute());
static_assert(Fixed<int, 2>{FromRatio(1, 4)} == Fixed<int, 2>{FromRatio(1, 4)}.Absolute());
static_assert(Fixed<int, 2>{FromRatio(1, 1)} == Fixed<int, 2>{FromRatio(-1, 1)}.Absolute());
static_assert(Fixed<int, 2>{FromRatio(1, 2)} == Fixed<int, 2>{FromRatio(-1, 2)}.Absolute());
static_assert(Fixed<int, 2>{FromRatio(1, 4)} == Fixed<int, 2>{FromRatio(-1, 4)}.Absolute());

static_assert(Fixed<int8_t, 7>::Max() == Fixed<int8_t, 7>::Max().Absolute());
static_assert(Fixed<int16_t, 15>::Max() == Fixed<int16_t, 15>::Max().Absolute());
static_assert(Fixed<int32_t, 31>::Max() == Fixed<int32_t, 31>::Max().Absolute());
static_assert(Fixed<int64_t, 63>::Max() == Fixed<int64_t, 63>::Max().Absolute());
static_assert(Fixed<uint8_t, 8>::Max() == Fixed<uint8_t, 8>::Max().Absolute());
static_assert(Fixed<uint16_t, 16>::Max() == Fixed<uint16_t, 16>::Max().Absolute());
static_assert(Fixed<uint32_t, 32>::Max() == Fixed<uint32_t, 32>::Max().Absolute());
static_assert(Fixed<uint64_t, 64>::Max() == Fixed<uint64_t, 64>::Max().Absolute());

static_assert(Fixed<int8_t, 7>::Max() == Fixed<int8_t, 7>::Min().Absolute());
static_assert(Fixed<int16_t, 15>::Max() == Fixed<int16_t, 15>::Min().Absolute());
static_assert(Fixed<int32_t, 31>::Max() == Fixed<int32_t, 31>::Min().Absolute());
static_assert(Fixed<int64_t, 63>::Max() == Fixed<int64_t, 63>::Min().Absolute());
static_assert(Fixed<uint8_t, 8>::Min() == Fixed<uint8_t, 8>::Min().Absolute());
static_assert(Fixed<uint16_t, 16>::Min() == Fixed<uint16_t, 16>::Min().Absolute());
static_assert(Fixed<uint32_t, 32>::Min() == Fixed<uint32_t, 32>::Min().Absolute());
static_assert(Fixed<uint64_t, 64>::Min() == Fixed<uint64_t, 64>::Min().Absolute());

}  // anonymous namespace

TEST(FuchsiaFixedPoint, Dummy) {
  // We don't have anything to test at runtime, but infra expects something.
  EXPECT_TRUE(true);
}
