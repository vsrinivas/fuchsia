// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#define PROVIDE_NUMERIC_LIMITS_UNSPECIALIZED
#include <fbl/limits.h>
#include <fbl/type_support.h>

#include <unittest.h>

#include <safeint/safe_conversions.h>
#include <safeint/safe_math.h>

#if defined(COMPILER_MSVC) && defined(ARCH_CPU_32_BITS)
#include <mmintrin.h>
#endif

using fbl::numeric_limits;
using safeint::CheckedNumeric;
using safeint::checked_cast;
using safeint::IsValueInRangeForNumericType;
using safeint::IsValueNegative;
using safeint::SizeT;
using safeint::StrictNumeric;
using safeint::saturated_cast;
using safeint::strict_cast;
using safeint::internal::MaxExponent;
using safeint::internal::RANGE_VALID;
using safeint::internal::RANGE_INVALID;
using safeint::internal::RANGE_OVERFLOW;
using safeint::internal::RANGE_UNDERFLOW;
using safeint::internal::SignedIntegerForSize;

// These tests deliberately cause arithmetic overflows. If the compiler is
// aggressive enough, it can const fold these overflows. Disable warnings about
// overflows for const expressions.
#if defined(OS_WIN)
#pragma warning(disable:4756)
#endif

// Helper macros to wrap displaying the conversion types and line numbers.
#define TEST_EXPECTED_VALIDITY(expected, actual)                           \
  EXPECT_EQ(expected, CheckedNumeric<Dst>(actual).validity(),              \
            "unexpected validity")

#define TEST_EXPECTED_VALUE(expected, actual)                              \
  EXPECT_EQ(static_cast<Dst>(expected),                                    \
            CheckedNumeric<Dst>(actual).ValueUnsafe(),                     \
            "unexpected value")

#define EXPECT_EQ2(v1, v2) \
        EXPECT_EQ(v1, v2, "EXPECT_EQ fail")

#define EXPECT_TRUE1(v1) \
        EXPECT_TRUE(v1, "EXPECT_TRUE fail")
#define EXPECT_FALSE1(v1) \
        EXPECT_FALSE(v1, "EXPECT_FALSE fail")

// Signed integer arithmetic.
template <typename Dst>
static bool TestSpecializedArithmetic(
    typename fbl::enable_if<numeric_limits<Dst>::is_integer &&
                                numeric_limits<Dst>::is_signed,
                            int>::type = 0) {
  typedef numeric_limits<Dst> DstLimits;
  BEGIN_TEST;
  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         -CheckedNumeric<Dst>(DstLimits::min()));
  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()).Abs());
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(-1).Abs());

  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::max()) + -1);
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) + -1);
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) + -DstLimits::max());

  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) - 1);
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) - -1);
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW,
      CheckedNumeric<Dst>(DstLimits::max()) - -DstLimits::max());
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) - DstLimits::max());

  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) * 2);

  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) / -1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(-1) / 2);

  // Modulus is legal only for integers.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() % 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  TEST_EXPECTED_VALUE(-1, CheckedNumeric<Dst>(-1) % 2);
  TEST_EXPECTED_VALIDITY(RANGE_INVALID, CheckedNumeric<Dst>(-1) % -2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) % 2);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(DstLimits::max()) % 2);
  // Test all the different modulus combinations.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst %= 1);
  END_TEST;
}

// Unsigned integer arithmetic.
template <typename Dst>
static bool TestSpecializedArithmetic(
    typename fbl::enable_if<numeric_limits<Dst>::is_integer &&
                                !numeric_limits<Dst>::is_signed,
                            int>::type = 0) {
  typedef numeric_limits<Dst> DstLimits;
  BEGIN_TEST;
  TEST_EXPECTED_VALIDITY(RANGE_VALID, -CheckedNumeric<Dst>(DstLimits::min()));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()).Abs());
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) + -1);
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) - 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) * 2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) / 2);
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()).UnsignedAbs());
  TEST_EXPECTED_VALIDITY(
      RANGE_VALID,
      CheckedNumeric<typename SignedIntegerForSize<Dst>::type>(
          fbl::numeric_limits<typename SignedIntegerForSize<Dst>::type>::min())
          .UnsignedAbs());

  // Modulus is legal only for integers.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() % 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) % 2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) % 2);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(DstLimits::max()) % 2);
  // Test all the different modulus combinations.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst %= 1);
  END_TEST;
}

// Generic arithmetic tests.
template <typename Dst>
static bool TestArithmetic(void* ctx) {
  typedef numeric_limits<Dst> DstLimits;

  BEGIN_TEST;
  EXPECT_EQ2(true, CheckedNumeric<Dst>().IsValid());
  EXPECT_EQ2(false,
            CheckedNumeric<Dst>(CheckedNumeric<Dst>(DstLimits::max()) *
                                DstLimits::max()).IsValid());
  EXPECT_EQ2(static_cast<Dst>(0), CheckedNumeric<Dst>().ValueOrDie());
  EXPECT_EQ2(static_cast<Dst>(0), CheckedNumeric<Dst>().ValueOrDefault(1));
  EXPECT_EQ2(static_cast<Dst>(1),
            CheckedNumeric<Dst>(CheckedNumeric<Dst>(DstLimits::max()) *
                                DstLimits::max()).ValueOrDefault(1));

  // Test the operator combinations.
  TEST_EXPECTED_VALUE(2, CheckedNumeric<Dst>(1) + CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) - CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) * CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(2, 1 + CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 - CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, 1 * CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, 1 / CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(2, CheckedNumeric<Dst>(1) + 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) - 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) * 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(2, checked_dst += 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst -= 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(1, checked_dst *= 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(1, checked_dst /= 1);

  // Generic negation.
  TEST_EXPECTED_VALUE(0, -CheckedNumeric<Dst>());
  TEST_EXPECTED_VALUE(-1, -CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, -CheckedNumeric<Dst>(-1));
  TEST_EXPECTED_VALUE(static_cast<Dst>(DstLimits::max() * -1),
                      -CheckedNumeric<Dst>(DstLimits::max()));

  // Generic absolute value.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>().Abs());
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1).Abs());
  TEST_EXPECTED_VALUE(DstLimits::max(),
                      CheckedNumeric<Dst>(DstLimits::max()).Abs());

  // Generic addition.
  TEST_EXPECTED_VALUE(1, (CheckedNumeric<Dst>() + 1));
  TEST_EXPECTED_VALUE(2, (CheckedNumeric<Dst>(1) + 1));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(-1) + 1));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) + 1);
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW, CheckedNumeric<Dst>(DstLimits::max()) + DstLimits::max());

  // Generic subtraction.
  TEST_EXPECTED_VALUE(-1, (CheckedNumeric<Dst>() - 1));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(1) - 1));
  TEST_EXPECTED_VALUE(-2, (CheckedNumeric<Dst>(-1) - 1));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::max()) - 1);

  // Generic multiplication.
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>() * 1));
  TEST_EXPECTED_VALUE(1, (CheckedNumeric<Dst>(1) * 1));
  TEST_EXPECTED_VALUE(-2, (CheckedNumeric<Dst>(-1) * 2));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(0) * 0));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(-1) * 0));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(0) * -1));
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW, CheckedNumeric<Dst>(DstLimits::max()) * DstLimits::max());

  // Generic division.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() / 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / 1);
  TEST_EXPECTED_VALUE(DstLimits::min() / 2,
                      CheckedNumeric<Dst>(DstLimits::min()) / 2);
  TEST_EXPECTED_VALUE(DstLimits::max() / 2,
                      CheckedNumeric<Dst>(DstLimits::max()) / 2);

  TestSpecializedArithmetic<Dst>();
  END_TEST;
}

#define TEST_ARITHMETIC(Dst) UNITTEST(#Dst, TestArithmetic<Dst>)

UNITTEST_START_TESTCASE(SafeNumerics_SignedIntegerMath)
TEST_ARITHMETIC(int8_t)
TEST_ARITHMETIC(int)
TEST_ARITHMETIC(intptr_t)
TEST_ARITHMETIC(intmax_t)
UNITTEST_END_TESTCASE(SafeNumerics_SignedIntegerMath,
                      "intmath", "SafeNumerics signed integer arithmetic tests",
                      NULL, NULL);

UNITTEST_START_TESTCASE(SafeNumerics_UnsignedIntegerMath)
TEST_ARITHMETIC(uint8_t)
TEST_ARITHMETIC(uint)
TEST_ARITHMETIC(uintptr_t)
TEST_ARITHMETIC(uintmax_t)
UNITTEST_END_TESTCASE(SafeNumerics_UnsignedIntegerMath,
                      "uintmath", "SafeNumerics unsigned integer arithmetic tests",
                      NULL, NULL);

// Enumerates the five different conversions types we need to test.
enum NumericConversionType {
  SIGN_PRESERVING_VALUE_PRESERVING,
  SIGN_PRESERVING_NARROW,
  SIGN_TO_UNSIGN_WIDEN_OR_EQUAL,
  SIGN_TO_UNSIGN_NARROW,
  UNSIGN_TO_SIGN_NARROW_OR_EQUAL,
};

// Template covering the different conversion tests.
template <typename Dst, typename Src, NumericConversionType conversion>
struct TestNumericConversion {};

// EXPECT_EQ2 wrappers providing specific detail on test failures.
#define TEST_EXPECTED_RANGE(expected, actual)                                  \
  EXPECT_EQ(expected,                                                          \
            safeint::internal::DstRangeRelationToSrcRange<Dst>(actual),        \
            "Conversion test failed")

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_PRESERVING_VALUE_PRESERVING> {
  static bool Test(void*) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    BEGIN_TEST;
                   // Integral to floating.
    static_assert((DstLimits::is_iec559 && SrcLimits::is_integer) ||
                  // Not floating to integral and...
                  (!(DstLimits::is_integer && SrcLimits::is_iec559) &&
                   // Same sign, same numeric, source is narrower or same.
                   ((SrcLimits::is_signed == DstLimits::is_signed &&
                    sizeof(Dst) >= sizeof(Src)) ||
                   // Or signed destination and source is smaller
                    (DstLimits::is_signed && sizeof(Dst) > sizeof(Src)))),
                  "Comparison must be sign preserving and value preserving");

    const CheckedNumeric<Dst> checked_dst = SrcLimits::max();
    TEST_EXPECTED_VALIDITY(RANGE_VALID, checked_dst);
    if (MaxExponent<Dst>::value > MaxExponent<Src>::value) {
      if (MaxExponent<Dst>::value >= MaxExponent<Src>::value * 2 - 1) {
        // At least twice larger type.
        TEST_EXPECTED_VALIDITY(RANGE_VALID, SrcLimits::max() * checked_dst);

      } else {  // Larger, but not at least twice as large.
        TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, SrcLimits::max() * checked_dst);
        TEST_EXPECTED_VALIDITY(RANGE_VALID, checked_dst + 1);
      }
    } else {  // Same width type.
      TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + 1);
    }

    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max() * static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
    } else if (numeric_limits<Src>::is_signed) {
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    }
    END_TEST;
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_PRESERVING_NARROW> {
  static bool Test(void*) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    BEGIN_TEST;
    static_assert(SrcLimits::is_signed == DstLimits::is_signed,
                  "Destination and source sign must be the same");
    static_assert(sizeof(Dst) < sizeof(Src) ||
                   (DstLimits::is_integer && SrcLimits::is_iec559),
                  "Destination must be narrower than source");

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst - SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::max() * -1);
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
      if (DstLimits::is_integer) {
        if (SrcLimits::digits < DstLimits::digits) {
          TEST_EXPECTED_RANGE(RANGE_OVERFLOW,
                              static_cast<Src>(DstLimits::max()));
        } else {
          TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(DstLimits::max()));
        }
        TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(DstLimits::min()));
      }
    } else if (SrcLimits::is_signed) {
      TEST_EXPECTED_VALUE(-1, checked_dst - static_cast<Src>(1));
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
    } else {
      TEST_EXPECTED_VALIDITY(RANGE_INVALID, checked_dst - static_cast<Src>(1));
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    }
    END_TEST;
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL> {
  static bool Test(void*) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    BEGIN_TEST;
    static_assert(sizeof(Dst) >= sizeof(Src),
                  "Destination must be equal or wider than source.");
    static_assert(SrcLimits::is_signed, "Source must be signed");
    static_assert(!DstLimits::is_signed, "Destination must be unsigned");

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(SrcLimits::max(), checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + static_cast<Src>(-1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + -SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, static_cast<Src>(-1));
    END_TEST;
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_TO_UNSIGN_NARROW> {
  static bool Test(void*) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    BEGIN_TEST;
    static_assert((DstLimits::is_integer && SrcLimits::is_iec559) ||
                   (sizeof(Dst) < sizeof(Src)),
                  "Destination must be narrower than source.");
    static_assert(SrcLimits::is_signed, "Source must be signed.");
    static_assert(!DstLimits::is_signed, "Destination must be unsigned.");

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + static_cast<Src>(-1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + -SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, static_cast<Src>(-1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::max() * -1);
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
      if (DstLimits::is_integer) {
        if (SrcLimits::digits < DstLimits::digits) {
          TEST_EXPECTED_RANGE(RANGE_OVERFLOW,
                              static_cast<Src>(DstLimits::max()));
        } else {
          TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(DstLimits::max()));
        }
        TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(DstLimits::min()));
      }
    } else {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
    }
    END_TEST;
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, UNSIGN_TO_SIGN_NARROW_OR_EQUAL> {
  static bool Test(void*) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    BEGIN_TEST;
    static_assert(sizeof(Dst) <= sizeof(Src),
                  "Destination must be narrower or equal to source.");
    static_assert(!SrcLimits::is_signed, "Source must be unsigned.");
    static_assert(DstLimits::is_signed, "Destination must be signed.");

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALUE(SrcLimits::min(), checked_dst + SrcLimits::min());

    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    END_TEST;
  }
};

#define TEST_NUMERIC_CONVERSION(d, s, t) \
    UNITTEST(#d " <--> " #s, (TestNumericConversion<d, s, t>::Test))

UNITTEST_START_TESTCASE(SafeNumerics_IntMinOperations)
TEST_NUMERIC_CONVERSION(int8_t,  int8_t,       SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(uint8_t, uint8_t,      SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(int8_t,  int,          SIGN_PRESERVING_NARROW)
TEST_NUMERIC_CONVERSION(uint8_t, unsigned int, SIGN_PRESERVING_NARROW)
TEST_NUMERIC_CONVERSION(uint8_t, int8_t,       SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(uint8_t, int,          SIGN_TO_UNSIGN_NARROW)
TEST_NUMERIC_CONVERSION(uint8_t, intmax_t,     SIGN_TO_UNSIGN_NARROW)
TEST_NUMERIC_CONVERSION(int8_t,  unsigned int, UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
TEST_NUMERIC_CONVERSION(int8_t,  uintmax_t,    UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
UNITTEST_END_TESTCASE(SafeNumerics_IntMinOperations,
                      "intmin_ops", "SafeNumerics IntMin operations",
                      NULL, NULL);

UNITTEST_START_TESTCASE(SafeNumerics_IntOperations)
TEST_NUMERIC_CONVERSION(int,          int,          SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(unsigned int, unsigned int, SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(int,          int8_t,       SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(unsigned int, uint8_t,      SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(int,          uint8_t,      SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(int,          intmax_t,     SIGN_PRESERVING_NARROW)
TEST_NUMERIC_CONVERSION(unsigned int, uintmax_t,    SIGN_PRESERVING_NARROW)
TEST_NUMERIC_CONVERSION(unsigned int, int,          SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(unsigned int, int8_t,       SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(unsigned int, intmax_t,     SIGN_TO_UNSIGN_NARROW)
TEST_NUMERIC_CONVERSION(int,          unsigned int, UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
TEST_NUMERIC_CONVERSION(int,          uintmax_t,    UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
UNITTEST_END_TESTCASE(SafeNumerics_IntOperations,
                      "int_ops", "SafeNumerics Int operations",
                      NULL, NULL);

UNITTEST_START_TESTCASE(SafeNumerics_IntMaxOperations)
TEST_NUMERIC_CONVERSION(intmax_t,  intmax_t,     SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(uintmax_t, uintmax_t,    SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(intmax_t,  int,          SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(uintmax_t, unsigned int, SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(intmax_t,  unsigned int, SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(intmax_t,  uint8_t,      SIGN_PRESERVING_VALUE_PRESERVING)
TEST_NUMERIC_CONVERSION(uintmax_t, int,          SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(uintmax_t, int8_t,       SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(intmax_t,  uintmax_t,    UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
UNITTEST_END_TESTCASE(SafeNumerics_IntMaxOperations,
                      "intmax_ops", "SafeNumerics IntMax operations",
                      NULL, NULL);

UNITTEST_START_TESTCASE(SafeNumerics_SizeTOperations)
TEST_NUMERIC_CONVERSION(size_t, int,    SIGN_TO_UNSIGN_WIDEN_OR_EQUAL)
TEST_NUMERIC_CONVERSION(int,    size_t, UNSIGN_TO_SIGN_NARROW_OR_EQUAL)
UNITTEST_END_TESTCASE(SafeNumerics_SizeTOperations,
                      "sizet_ops", "SafeNumerics SizeT operations",
                      NULL, NULL);

bool SafeNumerics_CastTests(void*) {
// MSVC catches and warns that we're forcing saturation in these tests.
// Since that's intentional, we need to shut this warning off.
#if defined(COMPILER_MSVC)
#pragma warning(disable : 4756)
#endif

  int small_positive = 1;
  int small_negative = -1;

  BEGIN_TEST;

  // Just test that the casts compile, since the other tests cover logic.
  EXPECT_EQ2(0, checked_cast<int>(static_cast<size_t>(0)));
  EXPECT_EQ2(0, strict_cast<int>(static_cast<char>(0)));
  EXPECT_EQ2(0, strict_cast<int>(static_cast<unsigned char>(0)));
  EXPECT_EQ2(0U, strict_cast<unsigned>(static_cast<unsigned char>(0)));
  EXPECT_EQ2(1ULL, static_cast<uint64_t>(StrictNumeric<size_t>(1U)));
  EXPECT_EQ2(1ULL, static_cast<uint64_t>(SizeT(1U)));
  EXPECT_EQ2(1U, static_cast<size_t>(StrictNumeric<unsigned>(1U)));

  EXPECT_TRUE1(CheckedNumeric<uint64_t>(StrictNumeric<unsigned>(1U)).IsValid());
  EXPECT_TRUE1(CheckedNumeric<int>(StrictNumeric<unsigned>(1U)).IsValid());
  EXPECT_FALSE1(CheckedNumeric<unsigned>(StrictNumeric<int>(-1)).IsValid());

  EXPECT_TRUE1(IsValueNegative(-1));
  EXPECT_TRUE1(IsValueNegative(numeric_limits<int>::min()));
  EXPECT_FALSE1(IsValueNegative(numeric_limits<unsigned>::min()));
  EXPECT_FALSE1(IsValueNegative(0));
  EXPECT_FALSE1(IsValueNegative(1));
  EXPECT_FALSE1(IsValueNegative(0u));
  EXPECT_FALSE1(IsValueNegative(1u));
  EXPECT_FALSE1(IsValueNegative(numeric_limits<int>::max()));
  EXPECT_FALSE1(IsValueNegative(numeric_limits<unsigned>::max()));

  // These casts and coercions will fail to compile:
  // EXPECT_EQ2(0, strict_cast<int>(static_cast<size_t>(0)));
  // EXPECT_EQ2(0, strict_cast<size_t>(static_cast<int>(0)));
  // EXPECT_EQ2(1ULL, StrictNumeric<size_t>(1));
  // EXPECT_EQ2(1, StrictNumeric<size_t>(1U));

  // Test various saturation corner cases.
  EXPECT_EQ2(saturated_cast<int>(small_negative),
            static_cast<int>(small_negative));
  EXPECT_EQ2(saturated_cast<int>(small_positive),
            static_cast<int>(small_positive));
  EXPECT_EQ2(saturated_cast<unsigned>(small_negative),
            static_cast<unsigned>(0));
  END_TEST;
}

bool SafeNumerics_IsValueInRangeForNumericType(void*) {
  BEGIN_TEST;
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint32_t>(0));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint32_t>(1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint32_t>(2));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint32_t>(-1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint32_t>(0xffffffffu));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint32_t>(UINT64_C(0xffffffff)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint32_t>(UINT64_C(0x100000000)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint32_t>(UINT64_C(0x100000001)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint32_t>(
      fbl::numeric_limits<int32_t>::min()));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint32_t>(
      fbl::numeric_limits<int64_t>::min()));

  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(0));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(2));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(-1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(0x7fffffff));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(0x7fffffffu));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(0x80000000u));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(0xffffffffu));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(INT64_C(0x80000000)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(INT64_C(0xffffffff)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(INT64_C(0x100000000)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(
      fbl::numeric_limits<int32_t>::min()));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int32_t>(
      static_cast<int64_t>(fbl::numeric_limits<int32_t>::min())));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(
      static_cast<int64_t>(fbl::numeric_limits<int32_t>::min()) - 1));
  EXPECT_FALSE1(IsValueInRangeForNumericType<int32_t>(
      fbl::numeric_limits<int64_t>::min()));

  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(0));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(2));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint64_t>(-1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(0xffffffffu));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(UINT64_C(0xffffffff)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(UINT64_C(0x100000000)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<uint64_t>(UINT64_C(0x100000001)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint64_t>(
      fbl::numeric_limits<int32_t>::min()));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint64_t>(INT64_C(-1)));
  EXPECT_FALSE1(IsValueInRangeForNumericType<uint64_t>(
      fbl::numeric_limits<int64_t>::min()));

  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(0));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(2));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(-1));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(0x7fffffff));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(0x7fffffffu));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(0x80000000u));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(0xffffffffu));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(INT64_C(0x80000000)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(INT64_C(0xffffffff)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(INT64_C(0x100000000)));
  EXPECT_TRUE1(
      IsValueInRangeForNumericType<int64_t>(INT64_C(0x7fffffffffffffff)));
  EXPECT_TRUE1(
      IsValueInRangeForNumericType<int64_t>(UINT64_C(0x7fffffffffffffff)));
  EXPECT_FALSE1(
      IsValueInRangeForNumericType<int64_t>(UINT64_C(0x8000000000000000)));
  EXPECT_FALSE1(
      IsValueInRangeForNumericType<int64_t>(UINT64_C(0xffffffffffffffff)));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(
      fbl::numeric_limits<int32_t>::min()));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(
      static_cast<int64_t>(fbl::numeric_limits<int32_t>::min())));
  EXPECT_TRUE1(IsValueInRangeForNumericType<int64_t>(
      fbl::numeric_limits<int64_t>::min()));
  END_TEST;
}

bool SafeNumerics_CompoundNumericOperations(void*) {
  BEGIN_TEST;
  CheckedNumeric<int> a = 1;
  CheckedNumeric<int> b = 2;
  CheckedNumeric<int> c = 3;
  CheckedNumeric<int> d = 4;
  a += b;
  EXPECT_EQ2(3, a.ValueOrDie());
  a -= c;
  EXPECT_EQ2(0, a.ValueOrDie());
  d /= b;
  EXPECT_EQ2(2, d.ValueOrDie());
  d *= d;
  EXPECT_EQ2(4, d.ValueOrDie());

  CheckedNumeric<int> too_large = fbl::numeric_limits<int>::max();
  EXPECT_TRUE1(too_large.IsValid());
  too_large += d;
  EXPECT_FALSE1(too_large.IsValid());
  too_large -= d;
  EXPECT_FALSE1(too_large.IsValid());
  too_large /= d;
  EXPECT_FALSE1(too_large.IsValid());
  END_TEST;
}

UNITTEST_START_TESTCASE(SafeNumerics)
UNITTEST("Cast", SafeNumerics_CastTests)
UNITTEST("In-range for type", SafeNumerics_IsValueInRangeForNumericType)
UNITTEST("Compound numeric ops", SafeNumerics_CompoundNumericOperations)
UNITTEST_END_TESTCASE(SafeNumerics,
                      "safenum", "Misc. SafeNumerics tests.",
                      NULL, NULL);
