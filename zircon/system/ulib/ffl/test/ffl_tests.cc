// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>

#include <ffl/expression.h>
#include <ffl/fixed.h>
#include <ffl/saturating_arithmetic.h>
#include <ffl/string.h>
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
using ffl::String;
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

template <typename Int>
static constexpr bool TestMethodsOnNegativeIntegers() {
  // Ceiling
  static_assert(-1 == Fixed<Int, 0>{-1}.Ceiling());
  static_assert(0 == Fixed<Int, 1>{FromRatio(-1, 2)}.Ceiling());

  static_assert(-2 == Fixed<Int, 2>{FromRatio(-8, 4)}.Ceiling());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-7, 4)}.Ceiling());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-5, 4)}.Ceiling());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-4, 4)}.Ceiling());
  static_assert(0 == Fixed<Int, 2>{FromRatio(-2, 4)}.Ceiling());

  // Floor
  static_assert(-1 == Fixed<Int, 0>{-1}.Floor());
  static_assert(-1 == Fixed<Int, 1>{FromRatio(-1, 2)}.Floor());

  static_assert(-2 == Fixed<Int, 2>{FromRatio(-8, 4)}.Floor());
  static_assert(-2 == Fixed<Int, 2>{FromRatio(-7, 4)}.Floor());
  static_assert(-2 == Fixed<Int, 2>{FromRatio(-5, 4)}.Floor());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-4, 4)}.Floor());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-3, 4)}.Floor());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-2, 4)}.Floor());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-1, 4)}.Floor());

  // Round
  static_assert(-1 == Fixed<Int, 0>{-1}.Round());
  static_assert(-1 == Fixed<Int, 1>{-1}.Round());

  static_assert(-2 == Fixed<Int, 2>{FromRatio(-8, 4)}.Round());
  static_assert(-2 == Fixed<Int, 2>{FromRatio(-7, 4)}.Round());
  static_assert(-2 == Fixed<Int, 2>{FromRatio(-6, 4)}.Round());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-5, 4)}.Round());
  static_assert(-1 == Fixed<Int, 2>{FromRatio(-4, 4)}.Round());
  static_assert(0 == Fixed<Int, 2>{FromRatio(-2, 4)}.Round());
  static_assert(0 == Fixed<Int, 2>{FromRatio(-1, 4)}.Round());
  static_assert(0 == Fixed<Int, 1>{FromRatio(-1, 2)}.Round());

  // Integral
  static_assert(Fixed<Int, 0>{FromRatio(-2, 1)} == Fixed<Int, 0>{FromRatio(-2, 1)}.Integral());
  static_assert(Fixed<Int, 0>{FromRatio(-1, 1)} == Fixed<Int, 0>{FromRatio(-1, 1)}.Integral());

  static_assert(Fixed<Int, 2>{FromRatio(-2, 1)} == Fixed<Int, 2>{FromRatio(-9, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(-2, 1)} == Fixed<Int, 2>{FromRatio(-8, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(-1, 1)} == Fixed<Int, 2>{FromRatio(-7, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(-1, 1)} == Fixed<Int, 2>{FromRatio(-5, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(-1, 1)} == Fixed<Int, 2>{FromRatio(-4, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(-3, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(-2, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(-1, 4)}.Integral());

  // Fraction
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(-2, 1)}.Fraction());
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(-1, 1)}.Fraction());

  static_assert(Fixed<Int, 2>{FromRatio(-1, 4)} == Fixed<Int, 2>{FromRatio(-9, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(0, 4)} == Fixed<Int, 2>{FromRatio(-8, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(-3, 4)} == Fixed<Int, 2>{FromRatio(-7, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(-1, 4)} == Fixed<Int, 2>{FromRatio(-5, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(0, 4)} == Fixed<Int, 2>{FromRatio(-4, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(-3, 4)} == Fixed<Int, 2>{FromRatio(-3, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(-2, 4)} == Fixed<Int, 2>{FromRatio(-2, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(-1, 4)} == Fixed<Int, 2>{FromRatio(-1, 4)}.Fraction());

  // Absolute
  static_assert(Fixed<Int, 2>{FromRatio(4, 4)} == Fixed<Int, 2>{FromRatio(-4, 4)}.Absolute());
  static_assert(Fixed<Int, 2>{FromRatio(2, 4)} == Fixed<Int, 2>{FromRatio(-2, 4)}.Absolute());
  static_assert(Fixed<Int, 2>{FromRatio(1, 4)} == Fixed<Int, 2>{FromRatio(-1, 4)}.Absolute());

  return true;
}

template <typename Int>
static constexpr bool TestMethods() {
  if constexpr (std::is_signed_v<Int>) {
    static_assert(TestMethodsOnNegativeIntegers<Int>());
  }

  // Ceiling
  static_assert(0 == Fixed<Int, 0>{0}.Ceiling());
  static_assert(1 == Fixed<Int, 0>{1}.Ceiling());
  static_assert(1 == Fixed<Int, 1>{FromRatio(1, 2)}.Ceiling());

  static_assert(0 == Fixed<Int, 2>{FromRatio(-3, 4)}.Ceiling());
  static_assert(0 == Fixed<Int, 2>{FromRatio(-2, 4)}.Ceiling());
  static_assert(0 == Fixed<Int, 2>{FromRatio(-1, 4)}.Ceiling());
  static_assert(0 == Fixed<Int, 2>{0}.Ceiling());
  static_assert(1 == Fixed<Int, 2>{FromRatio(1, 4)}.Ceiling());
  static_assert(1 == Fixed<Int, 2>{FromRatio(2, 4)}.Ceiling());
  static_assert(1 == Fixed<Int, 2>{FromRatio(3, 4)}.Ceiling());
  static_assert(1 == Fixed<Int, 2>{FromRatio(4, 4)}.Ceiling());
  static_assert(2 == Fixed<Int, 2>{FromRatio(5, 4)}.Ceiling());
  static_assert(2 == Fixed<Int, 2>{FromRatio(7, 4)}.Ceiling());
  static_assert(2 == Fixed<Int, 2>{FromRatio(8, 4)}.Ceiling());

  // Floor
  static_assert(0 == Fixed<Int, 0>{0}.Floor());
  static_assert(1 == Fixed<Int, 0>{1}.Floor());
  static_assert(0 == Fixed<Int, 1>{FromRatio(1, 2)}.Floor());

  static_assert(0 == Fixed<Int, 2>{0}.Floor());
  static_assert(0 == Fixed<Int, 2>{FromRatio(1, 4)}.Floor());
  static_assert(0 == Fixed<Int, 2>{FromRatio(2, 4)}.Floor());
  static_assert(0 == Fixed<Int, 2>{FromRatio(3, 4)}.Floor());
  static_assert(1 == Fixed<Int, 2>{FromRatio(5, 4)}.Floor());
  static_assert(1 == Fixed<Int, 2>{FromRatio(7, 4)}.Floor());
  static_assert(2 == Fixed<Int, 2>{FromRatio(8, 4)}.Floor());

  // Round
  static_assert(0 == Fixed<Int, 0>{0}.Round());
  static_assert(1 == Fixed<Int, 0>{1}.Round());
  static_assert(1 == Fixed<Int, 1>{1}.Round());

  static_assert(0 == Fixed<Int, 1>{FromRatio(1, 2)}.Round());
  static_assert(0 == Fixed<Int, 2>{FromRatio(1, 4)}.Round());
  static_assert(0 == Fixed<Int, 2>{FromRatio(2, 4)}.Round());
  static_assert(1 == Fixed<Int, 2>{FromRatio(4, 4)}.Round());
  static_assert(1 == Fixed<Int, 2>{FromRatio(5, 4)}.Round());
  static_assert(2 == Fixed<Int, 2>{FromRatio(6, 4)}.Round());
  static_assert(2 == Fixed<Int, 2>{FromRatio(7, 4)}.Round());
  static_assert(2 == Fixed<Int, 2>{FromRatio(8, 4)}.Round());

  // Integral
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(0, 1)}.Integral());
  static_assert(Fixed<Int, 0>{FromRatio(1, 1)} == Fixed<Int, 0>{FromRatio(1, 1)}.Integral());
  static_assert(Fixed<Int, 0>{FromRatio(2, 1)} == Fixed<Int, 0>{FromRatio(2, 1)}.Integral());

  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(0, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(1, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(2, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(3, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(1, 1)} == Fixed<Int, 2>{FromRatio(4, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(1, 1)} == Fixed<Int, 2>{FromRatio(5, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(1, 1)} == Fixed<Int, 2>{FromRatio(7, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(2, 1)} == Fixed<Int, 2>{FromRatio(8, 4)}.Integral());
  static_assert(Fixed<Int, 2>{FromRatio(2, 1)} == Fixed<Int, 2>{FromRatio(9, 4)}.Integral());

  // Fraction
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(0, 1)}.Fraction());
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(1, 1)}.Fraction());
  static_assert(Fixed<Int, 0>{FromRatio(0, 1)} == Fixed<Int, 0>{FromRatio(2, 1)}.Fraction());

  static_assert(Fixed<Int, 2>{FromRatio(0, 1)} == Fixed<Int, 2>{FromRatio(0, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(1, 4)} == Fixed<Int, 2>{FromRatio(1, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(2, 4)} == Fixed<Int, 2>{FromRatio(2, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(3, 4)} == Fixed<Int, 2>{FromRatio(3, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(0, 4)} == Fixed<Int, 2>{FromRatio(4, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(1, 4)} == Fixed<Int, 2>{FromRatio(5, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(3, 4)} == Fixed<Int, 2>{FromRatio(7, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(0, 4)} == Fixed<Int, 2>{FromRatio(8, 4)}.Fraction());
  static_assert(Fixed<Int, 2>{FromRatio(1, 4)} == Fixed<Int, 2>{FromRatio(9, 4)}.Fraction());

  // Absolute
  static_assert(Fixed<Int, 2>{FromRatio(1, 4)} == Fixed<Int, 2>{FromRatio(1, 4)}.Absolute());
  static_assert(Fixed<Int, 2>{FromRatio(2, 4)} == Fixed<Int, 2>{FromRatio(2, 4)}.Absolute());
  static_assert(Fixed<Int, 2>{FromRatio(4, 4)} == Fixed<Int, 2>{FromRatio(4, 4)}.Absolute());

  return true;
}

static_assert(TestMethods<int8_t>());
static_assert(TestMethods<int16_t>());
static_assert(TestMethods<int32_t>());
static_assert(TestMethods<int64_t>());
static_assert(TestMethods<uint8_t>());
static_assert(TestMethods<uint16_t>());
static_assert(TestMethods<uint32_t>());
static_assert(TestMethods<uint64_t>());

// Boundary cases with zero integral bits.
static_assert(1 == Fixed<int8_t, 7>::Max().Ceiling());
static_assert(1 == Fixed<int16_t, 15>::Max().Ceiling());
static_assert(1 == Fixed<int32_t, 31>::Max().Ceiling());
static_assert(1 == Fixed<int64_t, 63>::Max().Ceiling());
static_assert(1 == Fixed<uint8_t, 8>::Max().Ceiling());
static_assert(1 == Fixed<uint16_t, 16>::Max().Ceiling());
static_assert(1 == Fixed<uint32_t, 32>::Max().Ceiling());
static_assert(1 == Fixed<uint64_t, 64>::Max().Ceiling());

static_assert(-1 == Fixed<int8_t, 7>::Min().Ceiling());
static_assert(-1 == Fixed<int16_t, 15>::Min().Ceiling());
static_assert(-1 == Fixed<int32_t, 31>::Min().Ceiling());
static_assert(-1 == Fixed<int64_t, 63>::Min().Ceiling());
static_assert(0 == Fixed<uint8_t, 8>::Min().Ceiling());
static_assert(0 == Fixed<uint16_t, 16>::Min().Ceiling());
static_assert(0 == Fixed<uint32_t, 32>::Min().Ceiling());
static_assert(0 == Fixed<uint64_t, 64>::Min().Ceiling());

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

static_assert(Fixed<int8_t, 7>(0) == Fixed<int8_t, 7>::Max().Integral());
static_assert(Fixed<int16_t, 15>(0) == Fixed<int16_t, 15>::Max().Integral());
static_assert(Fixed<int32_t, 31>(0) == Fixed<int32_t, 31>::Max().Integral());
static_assert(Fixed<int64_t, 63>(0) == Fixed<int64_t, 63>::Max().Integral());
static_assert(Fixed<uint8_t, 8>(0) == Fixed<uint8_t, 8>::Max().Integral());
static_assert(Fixed<uint16_t, 16>(0) == Fixed<uint16_t, 16>::Max().Integral());
static_assert(Fixed<uint32_t, 32>(0) == Fixed<uint32_t, 32>::Max().Integral());
static_assert(Fixed<uint64_t, 64>(0) == Fixed<uint64_t, 64>::Max().Integral());

static_assert(Fixed<int8_t, 7>(-1) == Fixed<int8_t, 7>::Min().Integral());
static_assert(Fixed<int16_t, 15>(-1) == Fixed<int16_t, 15>::Min().Integral());
static_assert(Fixed<int32_t, 31>(-1) == Fixed<int32_t, 31>::Min().Integral());
static_assert(Fixed<int64_t, 63>(-1) == Fixed<int64_t, 63>::Min().Integral());
static_assert(Fixed<uint8_t, 8>(0) == Fixed<uint8_t, 8>::Min().Integral());
static_assert(Fixed<uint16_t, 16>(0) == Fixed<uint16_t, 16>::Min().Integral());
static_assert(Fixed<uint32_t, 32>(0) == Fixed<uint32_t, 32>::Min().Integral());
static_assert(Fixed<uint64_t, 64>(0) == Fixed<uint64_t, 64>::Min().Integral());

static_assert(Fixed<int, 31>{FromRatio(-1, 1)} == Fixed<int, 31>{FromRatio(-4, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(-1, 1)} == Fixed<int, 31>{FromRatio(-3, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(-1, 1)} == Fixed<int, 31>{FromRatio(-2, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(-1, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(0, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(1, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(2, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(3, 2)}.Integral());
static_assert(Fixed<int, 31>{FromRatio(0, 1)} == Fixed<int, 31>{FromRatio(4, 2)}.Integral());

static_assert(Fixed<int8_t, 7>::Max() == Fixed<int8_t, 7>::Max().Fraction());
static_assert(Fixed<int16_t, 15>::Max() == Fixed<int16_t, 15>::Max().Fraction());
static_assert(Fixed<int32_t, 31>::Max() == Fixed<int32_t, 31>::Max().Fraction());
static_assert(Fixed<int64_t, 63>::Max() == Fixed<int64_t, 63>::Max().Fraction());
static_assert(Fixed<uint8_t, 8>::Max() == Fixed<uint8_t, 8>::Max().Fraction());
static_assert(Fixed<uint16_t, 16>::Max() == Fixed<uint16_t, 16>::Max().Fraction());
static_assert(Fixed<uint32_t, 32>::Max() == Fixed<uint32_t, 32>::Max().Fraction());
static_assert(Fixed<uint64_t, 64>::Max() == Fixed<uint64_t, 64>::Max().Fraction());

template <typename Int, size_t Bits>
static constexpr Fixed<Int, Bits> FixedMinPlusOne() {
  // Can't do +1 because it's not representable when there are no integral bits.
  return Fixed<Int, Bits>::Min() - Fixed<Int, Bits>(-1);
}

static_assert(FixedMinPlusOne<int8_t, 7>() == Fixed<int8_t, 7>::Min().Fraction());
static_assert(FixedMinPlusOne<int16_t, 15>() == Fixed<int16_t, 15>::Min().Fraction());
static_assert(FixedMinPlusOne<int32_t, 31>() == Fixed<int32_t, 31>::Min().Fraction());
static_assert(FixedMinPlusOne<int64_t, 63>() == Fixed<int64_t, 63>::Min().Fraction());
static_assert(Fixed<uint8_t, 8>(0) == Fixed<uint8_t, 8>::Min().Fraction());
static_assert(Fixed<uint16_t, 16>(0) == Fixed<uint16_t, 16>::Min().Fraction());
static_assert(Fixed<uint32_t, 32>(0) == Fixed<uint32_t, 32>::Min().Fraction());
static_assert(Fixed<uint64_t, 64>(0) == Fixed<uint64_t, 64>::Min().Fraction());

static_assert(Fixed<int, 31>{FromRatio(0, 2)} == Fixed<int, 31>{FromRatio(-2, 2)}.Fraction());
static_assert(Fixed<int, 31>{FromRatio(-1, 2)} == Fixed<int, 31>{FromRatio(-1, 2)}.Fraction());
static_assert(Fixed<int, 31>{FromRatio(0, 2)} == Fixed<int, 31>{FromRatio(0, 2)}.Fraction());
static_assert(Fixed<int, 31>{FromRatio(1, 2)} == Fixed<int, 31>{FromRatio(1, 2)}.Fraction());

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

// Test that String is valid in constexpr context.
static_assert(Format(Fixed<uint8_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<int8_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<uint16_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<int16_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<uint32_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<int32_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<uint64_t, 0>::Min()).c_str() != nullptr);
static_assert(Format(Fixed<int64_t, 0>::Min()).c_str() != nullptr);

constexpr bool CStrEqualsData(const String& value) { return value.c_str() == value.data(); }

static_assert(CStrEqualsData(Format(Fixed<uint8_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<int8_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<uint16_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<int16_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<uint32_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<int32_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<uint64_t, 0>::Min())));
static_assert(CStrEqualsData(Format(Fixed<int64_t, 0>::Min())));

template <typename Integer, size_t FractionalBits>
constexpr String FormatHex(Fixed<Integer, FractionalBits> value) {
  return Format(value, String::Hex);
}

template <typename Integer, size_t FractionalBits>
constexpr String FormatRational(Fixed<Integer, FractionalBits> value) {
  return Format(value, String::DecRational);
}

}  // anonymous namespace

TEST(FuchsiaFixedPoint, Copy) {
  using F = Fixed<uint64_t, 0>;

  String string;
  EXPECT_STREQ(string.c_str(), "");

  string = Format(F::Max());
  EXPECT_STREQ(string.c_str(), "18446744073709551615.0");

  String string_copy = string;
  EXPECT_STREQ(string.c_str(), string_copy.c_str());
  EXPECT_NE(string.c_str(), string_copy.c_str());
}

TEST(FuchsiaFixedPoint, DecimalString) {
  {
    using F = Fixed<uint8_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "127.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "255.0");
  }
  {
    using F = Fixed<uint8_t, 4>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "7.9375");
    EXPECT_STREQ(Format(F::Max()).c_str(), "15.9375");
  }
  {
    using F = Fixed<uint8_t, 8>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.49609375");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.99609375");
  }
  {
    using F = Fixed<int8_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-128.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-64.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "63.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "127.0");
  }
  {
    using F = Fixed<int8_t, 4>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-8.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-4.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "3.9375");
    EXPECT_STREQ(Format(F::Max()).c_str(), "7.9375");
  }
  {
    using F = Fixed<int8_t, 7>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-1.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-0.5");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4921875");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9921875");
  }

  {
    using F = Fixed<uint16_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "32767.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "65535.0");
  }
  {
    using F = Fixed<uint16_t, 8>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "127.99609375");
    EXPECT_STREQ(Format(F::Max()).c_str(), "255.99609375");
  }
  {
    using F = Fixed<uint16_t, 16>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999847412");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999847412");
  }
  {
    using F = Fixed<int16_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-32768.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-16384.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "16383.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "32767.0");
  }
  {
    using F = Fixed<int16_t, 8>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-128.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-64.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "63.99609375");
    EXPECT_STREQ(Format(F::Max()).c_str(), "127.99609375");
  }
  {
    using F = Fixed<int16_t, 15>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-1.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-0.5");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999694824");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999694824");
  }

  {
    using F = Fixed<uint32_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "2147483647.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "4294967295.0");
  }
  {
    using F = Fixed<uint32_t, 16>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "32767.9999847412");
    EXPECT_STREQ(Format(F::Max()).c_str(), "65535.9999847412");
  }
  {
    using F = Fixed<uint32_t, 32>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999999997");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999999997");
  }
  {
    using F = Fixed<int32_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-2147483648.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-1073741824.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "1073741823.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "2147483647.0");
  }
  {
    using F = Fixed<int32_t, 16>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-32768.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-16384.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "16383.9999847412");
    EXPECT_STREQ(Format(F::Max()).c_str(), "32767.9999847412");
  }
  {
    using F = Fixed<int32_t, 31>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-1.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-0.5");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999999995");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999999995");
  }

  {
    using F = Fixed<uint64_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "9223372036854775807.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "18446744073709551615.0");
  }
  {
    using F = Fixed<uint64_t, 32>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "2147483647.9999999997");
    EXPECT_STREQ(Format(F::Max()).c_str(), "4294967295.9999999997");
  }
  {
    using F = Fixed<uint64_t, 64>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "0.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999999999");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999999999");
  }
  {
    using F = Fixed<int64_t, 0>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-9223372036854775808.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-4611686018427387904.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "4611686018427387903.0");
    EXPECT_STREQ(Format(F::Max()).c_str(), "9223372036854775807.0");
  }
  {
    using F = Fixed<int64_t, 32>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-2147483648.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-1073741824.0");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "1073741823.9999999997");
    EXPECT_STREQ(Format(F::Max()).c_str(), "2147483647.9999999997");
  }
  {
    using F = Fixed<int64_t, 63>;
    EXPECT_STREQ(Format(F::Min()).c_str(), "-1.0");
    EXPECT_STREQ(Format(F{F::Min() / 2}).c_str(), "-0.5");
    EXPECT_STREQ(Format(F{F::Max() / 2}).c_str(), "0.4999999999");
    EXPECT_STREQ(Format(F::Max()).c_str(), "0.9999999999");
  }
}

TEST(FuchsiaFixedPoint, RationalString) {
  {
    using F = Fixed<uint8_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "127+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "255+0/1");
  }
  {
    using F = Fixed<uint8_t, 4>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/16");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/16");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "7+15/16");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "15+15/16");
  }
  {
    using F = Fixed<uint8_t, 8>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/256");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/256");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+127/256");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+255/256");
  }
  {
    using F = Fixed<int8_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-128-0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-64-0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "63+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "127+0/1");
  }
  {
    using F = Fixed<int8_t, 4>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-8-0/16");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-4-0/16");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "3+15/16");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "7+15/16");
  }
  {
    using F = Fixed<int8_t, 7>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-1-0/128");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-0-64/128");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+63/128");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+127/128");
  }

  {
    using F = Fixed<uint16_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "32767+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "65535+0/1");
  }
  {
    using F = Fixed<uint16_t, 8>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/256");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/256");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "127+255/256");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "255+255/256");
  }
  {
    using F = Fixed<uint16_t, 16>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/65536");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/65536");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+32767/65536");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+65535/65536");
  }
  {
    using F = Fixed<int16_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-32768-0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-16384-0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "16383+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "32767+0/1");
  }
  {
    using F = Fixed<int16_t, 8>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-128-0/256");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-64-0/256");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "63+255/256");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "127+255/256");
  }
  {
    using F = Fixed<int16_t, 15>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-1-0/32768");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-0-16384/32768");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+16383/32768");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+32767/32768");
  }

  {
    using F = Fixed<uint32_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "2147483647+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "4294967295+0/1");
  }
  {
    using F = Fixed<uint32_t, 16>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/65536");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/65536");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "32767+65535/65536");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "65535+65535/65536");
  }
  {
    using F = Fixed<uint32_t, 32>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+2147483647/4294967296");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+4294967295/4294967296");
  }
  {
    using F = Fixed<int32_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-2147483648-0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-1073741824-0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "1073741823+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "2147483647+0/1");
  }
  {
    using F = Fixed<int32_t, 16>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-32768-0/65536");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-16384-0/65536");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "16383+65535/65536");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "32767+65535/65536");
  }
  {
    using F = Fixed<int32_t, 31>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-1-0/2147483648");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-0-1073741824/2147483648");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "0+1073741823/2147483648");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+2147483647/2147483648");
  }

  {
    using F = Fixed<uint64_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "9223372036854775807+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "18446744073709551615+0/1");
  }
  {
    using F = Fixed<uint64_t, 32>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "2147483647+4294967295/4294967296");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "4294967295+4294967295/4294967296");
  }
  {
    using F = Fixed<uint64_t, 64>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "0+0/18446744073709551616");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "0+0/18446744073709551616");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(),
                 "0+9223372036854775807/18446744073709551616");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+18446744073709551615/18446744073709551616");
  }
  {
    using F = Fixed<int64_t, 0>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-9223372036854775808-0/1");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-4611686018427387904-0/1");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "4611686018427387903+0/1");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "9223372036854775807+0/1");
  }
  {
    using F = Fixed<int64_t, 32>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-2147483648-0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(), "-1073741824-0/4294967296");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(), "1073741823+4294967295/4294967296");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "2147483647+4294967295/4294967296");
  }
  {
    using F = Fixed<int64_t, 63>;
    EXPECT_STREQ(FormatRational(F::Min()).c_str(), "-1-0/9223372036854775808");
    EXPECT_STREQ(FormatRational(F{F::Min() / 2}).c_str(),
                 "-0-4611686018427387904/9223372036854775808");
    EXPECT_STREQ(FormatRational(F{F::Max() / 2}).c_str(),
                 "0+4611686018427387903/9223372036854775808");
    EXPECT_STREQ(FormatRational(F::Max()).c_str(), "0+9223372036854775807/9223372036854775808");
  }
}

template <typename Int8OrUint8>
void TestHexStringInt8() {
  {
    using F = Fixed<Int8OrUint8, 0>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x03)).c_str(), "3.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x23)).c_str(), "23.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaa)).c_str(), "aa.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xff)).c_str(), "ff.0");
  }
  {
    using F = Fixed<Int8OrUint8, 1>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x03)).c_str(), "1.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x23)).c_str(), "11.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaa)).c_str(), "55.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xff)).c_str(), "7f.8");
  }
  {
    using F = Fixed<Int8OrUint8, 4>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x03)).c_str(), "0.3");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x23)).c_str(), "2.3");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaa)).c_str(), "a.a");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xff)).c_str(), "f.f");
  }
  {
    using F = Fixed<Int8OrUint8, 7>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x03)).c_str(), "0.06");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x23)).c_str(), "0.46");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaa)).c_str(), "1.54");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xff)).c_str(), "1.fe");
  }
  if constexpr (std::is_unsigned_v<Int8OrUint8>) {
    using F = Fixed<Int8OrUint8, 8>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x03)).c_str(), "0.03");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x23)).c_str(), "0.23");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaa)).c_str(), "0.aa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xff)).c_str(), "0.ff");
  }
}

TEST(FuchsiaFixedPoint, HexStringInt8) { TestHexStringInt8<int8_t>(); }
TEST(FuchsiaFixedPoint, HexStringUint8) { TestHexStringInt8<uint8_t>(); }

template <typename Int16OrUint16>
void TestHexStringInt16() {
  {
    using F = Fixed<Int16OrUint16, 0>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0203)).c_str(), "203.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x3333)).c_str(), "3333.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaa)).c_str(), "aaaa.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffff)).c_str(), "ffff.0");
  }
  {
    using F = Fixed<Int16OrUint16, 1>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0203)).c_str(), "101.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x3333)).c_str(), "1999.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaa)).c_str(), "5555.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffff)).c_str(), "7fff.8");
  }
  {
    using F = Fixed<Int16OrUint16, 8>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0203)).c_str(), "2.03");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x3333)).c_str(), "33.33");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaa)).c_str(), "aa.aa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffff)).c_str(), "ff.ff");
  }
  {
    using F = Fixed<Int16OrUint16, 15>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0203)).c_str(), "0.0406");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x3333)).c_str(), "0.6666");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaa)).c_str(), "1.5554");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffff)).c_str(), "1.fffe");
  }
  if constexpr (std::is_unsigned_v<Int16OrUint16>) {
    using F = Fixed<Int16OrUint16, 16>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0203)).c_str(), "0.0203");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x3333)).c_str(), "0.3333");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaa)).c_str(), "0.aaaa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffff)).c_str(), "0.ffff");
  }
}

TEST(FuchsiaFixedPoint, HexStringInt16) { TestHexStringInt16<int16_t>(); }
TEST(FuchsiaFixedPoint, HexStringUint16) { TestHexStringInt16<uint16_t>(); }

template <typename Int32OrUint32>
void TestHexStringInt32() {
  {
    using F = Fixed<Int32OrUint32, 0>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00020003)).c_str(), "20003.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x20203030)).c_str(), "20203030.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaa)).c_str(), "aaaaaaaa.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffff)).c_str(), "ffffffff.0");
  }
  {
    using F = Fixed<Int32OrUint32, 1>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00020003)).c_str(), "10001.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x20203030)).c_str(), "10101818.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaa)).c_str(), "55555555.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffff)).c_str(), "7fffffff.8");
  }
  {
    using F = Fixed<Int32OrUint32, 16>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00020003)).c_str(), "2.0003");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x20203030)).c_str(), "2020.303");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaa)).c_str(), "aaaa.aaaa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffff)).c_str(), "ffff.ffff");
  }
  {
    using F = Fixed<Int32OrUint32, 31>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00020003)).c_str(), "0.00040006");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x20203030)).c_str(), "0.4040606");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaa)).c_str(), "1.55555554");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffff)).c_str(), "1.fffffffe");
  }
  if constexpr (std::is_unsigned_v<Int32OrUint32>) {
    using F = Fixed<Int32OrUint32, 32>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x00020003)).c_str(), "0.00020003");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x20203030)).c_str(), "0.2020303");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaa)).c_str(), "0.aaaaaaaa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffff)).c_str(), "0.ffffffff");
  }
}

TEST(FuchsiaFixedPoint, HexStringInt32) { TestHexStringInt32<int32_t>(); }
TEST(FuchsiaFixedPoint, HexStringUint32) { TestHexStringInt32<uint32_t>(); }

template <typename Int64OrUint64>
void TestHexStringInt64() {
  {
    using F = Fixed<Int64OrUint64, 0>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000000000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000200000003)).c_str(), "200000003.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x2020202030303030)).c_str(), "2020202030303030.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaaaaaaaaaa)).c_str(), "aaaaaaaaaaaaaaaa.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffffffffffff)).c_str(), "ffffffffffffffff.0");
  }
  {
    using F = Fixed<Int64OrUint64, 1>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000000000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000200000003)).c_str(), "100000001.8");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x2020202030303030)).c_str(), "1010101018181818.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaaaaaaaaaa)).c_str(), "5555555555555555.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffffffffffff)).c_str(), "7fffffffffffffff.8");
  }
  {
    using F = Fixed<Int64OrUint64, 32>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000000000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000200000003)).c_str(), "2.00000003");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x2020202030303030)).c_str(), "20202020.3030303");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaaaaaaaaaa)).c_str(), "aaaaaaaa.aaaaaaaa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffffffffffff)).c_str(), "ffffffff.ffffffff");
  }
  {
    using F = Fixed<Int64OrUint64, 63>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000000000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000200000003)).c_str(), "0.0000000400000006");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x2020202030303030)).c_str(), "0.404040406060606");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaaaaaaaaaa)).c_str(), "1.5555555555555554");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffffffffffff)).c_str(), "1.fffffffffffffffe");
  }
  if constexpr (std::is_unsigned_v<Int64OrUint64>) {
    using F = Fixed<Int64OrUint64, 64>;
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000000000000)).c_str(), "0.0");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x0000000200000003)).c_str(), "0.0000000200000003");
    EXPECT_STREQ(FormatHex(F::FromRaw(0x2020202030303030)).c_str(), "0.202020203030303");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xaaaaaaaaaaaaaaaa)).c_str(), "0.aaaaaaaaaaaaaaaa");
    EXPECT_STREQ(FormatHex(F::FromRaw(0xffffffffffffffff)).c_str(), "0.ffffffffffffffff");
  }
}

TEST(FuchsiaFixedPoint, HexStringInt64) { TestHexStringInt64<int64_t>(); }
TEST(FuchsiaFixedPoint, HexStringUint64) { TestHexStringInt64<uint64_t>(); }

TEST(FuchsiaFixedPoint, StringLimitedFractionalDigits) {
  {
    auto x = Fixed<uint64_t, 64>::Max();
    // Decimal.
    EXPECT_STREQ(Format(x).c_str(), "0.9999999999");
    EXPECT_STREQ(Format(x, String::Dec, 0).c_str(), "0");
    EXPECT_STREQ(Format(x, String::Dec, 1).c_str(), "0.9");
    EXPECT_STREQ(Format(x, String::Dec, 5).c_str(), "0.99999");
    EXPECT_STREQ(Format(x, String::Dec, 10).c_str(), "0.9999999999");
    EXPECT_STREQ(Format(x, String::Dec, 100).c_str(),
                 "0.99999999999999999994578989137572477829962735");
    // Hex is not affected.
    EXPECT_STREQ(FormatHex(x).c_str(), "0.ffffffffffffffff");
    EXPECT_STREQ(Format(x, String::Hex, 1).c_str(), "0.ffffffffffffffff");
    EXPECT_STREQ(Format(x, String::Hex, 5).c_str(), "0.ffffffffffffffff");
    EXPECT_STREQ(Format(x, String::Hex, 16).c_str(), "0.ffffffffffffffff");
    EXPECT_STREQ(Format(x, String::Hex, 17).c_str(), "0.ffffffffffffffff");
    EXPECT_STREQ(Format(x, String::Hex, 100).c_str(), "0.ffffffffffffffff");
  }
  {
    auto x = Fixed<uint64_t, 4>::Max();
    // Decimal.
    EXPECT_STREQ(Format(x).c_str(), "1152921504606846975.9375");
    EXPECT_STREQ(Format(x, String::Dec, 0).c_str(), "1152921504606846975");
    EXPECT_STREQ(Format(x, String::Dec, 1).c_str(), "1152921504606846975.9");
    EXPECT_STREQ(Format(x, String::Dec, 5).c_str(), "1152921504606846975.9375");
    // Hex is not affected.
    EXPECT_STREQ(FormatHex(x).c_str(), "fffffffffffffff.f");
    EXPECT_STREQ(Format(x, String::Hex, 1).c_str(), "fffffffffffffff.f");
    EXPECT_STREQ(Format(x, String::Hex, 5).c_str(), "fffffffffffffff.f");
  }
}

TEST(FuchsiaFixedPoint, StringOstream) {
  auto x = Fixed<uint16_t, 16>::Max();

  {
    // Default is 6 digits of precision.
    std::stringstream ss;
    ss << x;
    EXPECT_EQ(ss.str(), "0.999984");
  }
  {
    std::stringstream ss;
    ss << std::setprecision(2) << x;
    EXPECT_EQ(ss.str(), "0.99");
  }
  {
    std::stringstream ss;
    ss << std::setprecision(0) << x;
    EXPECT_EQ(ss.str(), "0");
  }
  {
    std::stringstream ss;
    ss << String::Dec << std::setprecision(2) << x;
    EXPECT_EQ(ss.str(), "0.99");
  }
  {
    std::stringstream ss;
    ss << String::Hex << x;
    EXPECT_EQ(ss.str(), "0.ffff");
  }
  {
    std::stringstream ss;
    ss << String::Hex << std::showbase << x;
    EXPECT_EQ(ss.str(), "0x0.ffff");
  }
  {
    std::stringstream ss;
    ss << String::DecRational << x;
    EXPECT_EQ(ss.str(), "0+65535/65536");
  }
}
