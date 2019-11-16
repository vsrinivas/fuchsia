// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

#include <ffl/fixed.h>
#include <ffl/saturating_arithmetic.h>
#include <zxtest/zxtest.h>

namespace {

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
        static_assert_if(T::Max() * U{+2} == Offset<R>(R::Max(), -3), U::Format::IntegralBits == 2);
        static_assert_if(T::Min() * U{+2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Max() * U{-2} == R::Min(), U::Format::IntegralBits > 1);
        static_assert_if(T::Min() * U{-2} == R::Max(), U::Format::IntegralBits > 1 && !Truncating);

        static_assert_if(T{+2} * U::Max() == Offset<R>(R::Max(), -1),
                         T::Format::IntegralBits > 2 && !Truncating);
        static_assert_if(T{+2} * U::Max() == Offset<R>(R::Max(), -3), T::Format::IntegralBits == 2);
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
                         !ImpreciseOne && U::Format::IntegralBits == 2);
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

static_assert(1 == Fixed<int, 0>{1}.Ceiling());
static_assert(1 == Fixed<int, 1>{FromRatio(1, 2)}.Ceiling());
static_assert(1 == Fixed<int, 2>{FromRatio(1, 2)}.Ceiling());
static_assert(1 == Fixed<int, 2>{FromRatio(1, 4)}.Ceiling());
static_assert(0 == Fixed<int, 1>{FromRatio(-1, 2)}.Ceiling());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 2)}.Ceiling());
static_assert(0 == Fixed<int, 2>{FromRatio(-1, 4)}.Ceiling());
static_assert(-1 == Fixed<int, 0>{-1}.Ceiling());

static_assert(1 == Fixed<int, 0>{1}.Floor());
static_assert(0 == Fixed<int, 1>{FromRatio(1, 2)}.Floor());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 2)}.Floor());
static_assert(0 == Fixed<int, 2>{FromRatio(1, 4)}.Floor());
static_assert(-1 == Fixed<int, 1>{FromRatio(-1, 2)}.Floor());
static_assert(-1 == Fixed<int, 2>{FromRatio(-1, 2)}.Floor());
static_assert(-1 == Fixed<int, 2>{FromRatio(-1, 4)}.Floor());
static_assert(-1 == Fixed<int, 0>{-1}.Floor());

}  // anonymous namespace

TEST(FuchsiaFixedPoint, Dummy) {
  // We don't have anything to test at runtime, but infra expects something.
  EXPECT_TRUE(true);
}
