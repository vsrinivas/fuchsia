// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/bit.h>

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <gtest/gtest.h>

namespace {
#if __SIZEOF_INT128__ == 16
using uint128_t = unsigned __int128;
#else  // If no 128 bit is supported on the target platform, just provide a filler.
using uint128_t = unsigned;
#endif

// Types have overriden operator& to double check that |cpp20::addressof| is used intead of &.
struct A {
  constexpr A(const A&) = default;

  constexpr A* operator&() const { return nullptr; }
  std::array<uint8_t, 8> bytes;
};

struct B {
  constexpr B(const B&) = default;
  constexpr B* operator&() const { return nullptr; }
  uint64_t number;
};

static_assert(sizeof(A) == sizeof(B), "Test types must have the same size.");
static_assert(std::is_trivially_copyable<A>::value, "A must be trivially copyable.");
static_assert(std::is_trivially_copyable<B>::value, "B must be trivially copyable.");

TEST(BitCastTest, BitContentsMatch) {
  B b = {.number = 32};
  auto result = cpp20::bit_cast<A>(b);
  EXPECT_TRUE(memcmp(cpp17::addressof(result), cpp17::addressof(b), sizeof(b)) == 0);
}

template <typename From, typename To,
          std::enable_if_t<std::is_unsigned<From>::value && std::is_signed<To>::value, bool> = true>
bool CheckBitCast() {
  // This should translate to -1 on a signed type.
  {
    From from = std::numeric_limits<From>::max();
    To to = cpp20::bit_cast<To>(from);
    From from_2 = cpp20::bit_cast<From>(to);

    if (!(memcmp(&to, &from, sizeof(From)) == 0 && memcmp(&from, &from_2, sizeof(From)) == 0)) {
      EXPECT_TRUE(false) << "bit_cast failed to convert from unsigned to signed.";
      return false;
    }
  }

  // Now the other way around.
  {
    To from = -1;
    From to = cpp20::bit_cast<From>(from);
    From from_2 = cpp20::bit_cast<From>(to);

    if (!(memcmp(&to, &from, sizeof(From)) == 0 && memcmp(&from, &from_2, sizeof(From)) == 0)) {
      EXPECT_TRUE(false)
          << "bit_cast failed to convert from signed to unsigned with negative value.";
      return false;
    }
  }
  return true;
}

TEST(BitCastTest, DiffersFromStaticCast) {
  EXPECT_TRUE((CheckBitCast<unsigned, int>()));
  EXPECT_TRUE((CheckBitCast<unsigned long, long>()));
  EXPECT_TRUE((CheckBitCast<unsigned long long, long long>()));
  EXPECT_TRUE((CheckBitCast<uint8_t, int8_t>()));
  EXPECT_TRUE((CheckBitCast<uint16_t, int16_t>()));
  EXPECT_TRUE((CheckBitCast<uint32_t, int32_t>()));
  EXPECT_TRUE((CheckBitCast<uint64_t, int64_t>()));
  EXPECT_TRUE((CheckBitCast<uint64_t, double>()));
}

#if !defined(LIB_STDCOMPAT_NONCONSTEXPR_BITCAST)

constexpr bool BitCastIsConstexpr() {
  B b = {.number = 32};
  auto result = cpp20::bit_cast<B>(cpp20::bit_cast<A>(b));
  return result.number == b.number;
}

TEST(BitCastTest, WorksInConstexprContext) {
  static_assert(BitCastIsConstexpr(), "Failed to evaluate 'cpp20::bit_cast' in constexpr context.");
}

#endif

#if __cpp_lib_bit_cast >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(BitCastTest, IsAliasForStdBitCastIfAvailable) {
  static_assert(&std::bit_cast == &cpp20::bit_cast,
                "'bit_cast' polyfill must be an alias when std::bit_cast is available.");
}

#endif

template <typename T>
constexpr bool CheckCountZeroFromLeft() {
  static_assert(cpp20::countl_zero(static_cast<T>(0)) == (std::numeric_limits<T>::digits));
  static_assert(cpp20::countl_zero(static_cast<T>(-1)) == 0);
  for (T i = 1; i < std::numeric_limits<T>::digits; ++i) {
    T t = 1;
    t = t << (i - 1);
    if (static_cast<T>(cpp20::countl_zero(t)) != (std::numeric_limits<T>::digits - i)) {
      return false;
    }
  }
  return true;
}

TEST(BitOpsTest, CountLZeroReturnsRightValueForPowersOfTwo) {
  // Platform
  static_assert(CheckCountZeroFromLeft<unsigned>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<unsigned long>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<unsigned long long>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<unsigned char>(), "Failed countl_zero for unsigned.");

  // Fixed size
  static_assert(CheckCountZeroFromLeft<uint8_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint16_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint32_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint64_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint128_t>(), "Failed countl_zero for unsigned.");

  // Minimum size
  static_assert(CheckCountZeroFromLeft<uint_least8_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_least16_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_least32_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_least64_t>(), "Failed countl_zero for unsigned.");

  // Fast
  static_assert(CheckCountZeroFromLeft<uint_fast8_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_fast16_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_fast32_t>(), "Failed countl_zero for unsigned.");
  static_assert(CheckCountZeroFromLeft<uint_fast64_t>(), "Failed countl_zero for unsigned.");
}

template <typename T>
constexpr bool CheckCountZeroFromRight() {
  static_assert(cpp20::countr_zero(static_cast<T>(0)) == (std::numeric_limits<T>::digits));
  static_assert(cpp20::countr_zero(static_cast<T>(-1)) == 0);
  for (T i = 1; i < std::numeric_limits<T>::digits; ++i) {
    T t = 1;
    t = t << (i - 1);
    if (static_cast<T>(cpp20::countr_zero(t)) != i - 1) {
      return false;
    }
  }
  return true;
}

TEST(BitOpsTest, CountRZeroReturnsRightValueForPowersOfTwo) {
  static_assert(CheckCountZeroFromRight<unsigned>(), "Failed countr_zero for unsigned.");
  static_assert(CheckCountZeroFromRight<unsigned long>(), "Failed countr_zero for unsigned long.");
  static_assert(CheckCountZeroFromRight<unsigned long long>(),
                "Failed countr_zero for unsigned long long.");
  static_assert(CheckCountZeroFromRight<unsigned char>(), "Failed countr_zero for unsigned char.");
  static_assert(CheckCountZeroFromRight<uint8_t>(), "Failed countr_zero for uint8_t.");
  static_assert(CheckCountZeroFromRight<uint16_t>(), "Failed countr_zero for uint16_t.");
  static_assert(CheckCountZeroFromRight<uint32_t>(), "Failed countr_zero for uint32_t.");
  static_assert(CheckCountZeroFromRight<uint64_t>(), "Failed countr_zero for uint64_t.");
  static_assert(CheckCountZeroFromRight<uint128_t>(), "Failed countl_zero for unsigned.");

  static_assert(CheckCountZeroFromRight<uint_least8_t>(), "Failed countr_zero for uint_least8_t.");
  static_assert(CheckCountZeroFromRight<uint_least16_t>(),
                "Failed countr_zero for uint_least16_t.");
  static_assert(CheckCountZeroFromRight<uint_least32_t>(),
                "Failed countr_zero for uint_least32_t.");
  static_assert(CheckCountZeroFromRight<uint_least64_t>(),
                "Failed countr_zero for uint_least64_t.");
  static_assert(CheckCountZeroFromRight<uint_fast8_t>(), "Failed countr_zero for uint_fast8_t.");
  static_assert(CheckCountZeroFromRight<uint_fast16_t>(), "Failed countr_zero for uint_fast16_t.");
  static_assert(CheckCountZeroFromRight<uint_fast32_t>(), "Failed countr_zero for uint_fast32_t.");
  static_assert(CheckCountZeroFromRight<uint_fast64_t>(), "Failed countr_zero for uint_fast64_t.");
}

template <typename T>
constexpr bool CheckCountOneFromLeft() {
  static_assert(cpp20::countl_one(static_cast<T>(-1)) == (std::numeric_limits<T>::digits));
  static_assert(cpp20::countl_one(static_cast<T>(0)) == 0);
  for (T i = 1; i < std::numeric_limits<T>::digits; ++i) {
    T t = 1;
    t = t << (i - 1);
    if (static_cast<T>(cpp20::countl_one(static_cast<T>(~t))) !=
        (std::numeric_limits<T>::digits - i)) {
      return false;
    }
  }
  return true;
}

TEST(BitOpsTest, CountLOneReturnsRightValueForPowersOfTwo) {
  static_assert(CheckCountOneFromLeft<unsigned>(), "Failed countl_one for unsigned.");
  static_assert(CheckCountOneFromLeft<unsigned long>(), "Failed countl_one for unsigned long.");
  static_assert(CheckCountOneFromLeft<unsigned long long>(),
                "Failed countl_one for unsigned long long.");
  static_assert(CheckCountOneFromLeft<unsigned char>(), "Failed countl_one for unsigned char.");

  static_assert(CheckCountOneFromLeft<uint8_t>(), "Failed countl_one for uint8_t.");
  static_assert(CheckCountOneFromLeft<uint16_t>(), "Failed countl_one for uint16_t.");
  static_assert(CheckCountOneFromLeft<uint32_t>(), "Failed countl_one for uint32_t.");
  static_assert(CheckCountOneFromLeft<uint64_t>(), "Failed countl_one for uint64_t.");
  static_assert(CheckCountOneFromLeft<uint128_t>(), "Failed countl_one for uint_fast64_t.");

  static_assert(CheckCountOneFromLeft<uint_least8_t>(), "Failed countl_one for uint_least8_t.");
  static_assert(CheckCountOneFromLeft<uint_least16_t>(), "Failed countl_one for uint_least16_t.");
  static_assert(CheckCountOneFromLeft<uint_least32_t>(), "Failed countl_one for uint_least32_t.");
  static_assert(CheckCountOneFromLeft<uint_least64_t>(), "Failed countl_one for uint_least64_t.");

  static_assert(CheckCountOneFromLeft<uint_fast8_t>(), "Failed countl_one for uint_fast8_t.");
  static_assert(CheckCountOneFromLeft<uint_fast16_t>(), "Failed countl_one for uint_fast16_t.");
  static_assert(CheckCountOneFromLeft<uint_fast32_t>(), "Failed countl_one for uint_fast32_t.");
  static_assert(CheckCountOneFromLeft<uint_fast64_t>(), "Failed countl_one for uint_fast64_t.");
}

template <typename T>
constexpr bool CheckCountOneFromRight() {
  static_assert(cpp20::countr_one(static_cast<T>(0)) == 0);
  static_assert(cpp20::countr_one(static_cast<T>(-1)) == std::numeric_limits<T>::digits);
  for (T i = 1; i < std::numeric_limits<T>::digits; ++i) {
    T t = 1;
    t = t << (i - 1);
    if (static_cast<T>(cpp20::countr_one(static_cast<T>(~t))) != i - 1) {
      return false;
    }
  }
  return true;
}

TEST(BitOpsTest, CountROneReturnsRightValueForPowersOfTwo) {
  static_assert(CheckCountOneFromRight<unsigned>(), "Failed countr_one for unsigned.");
  static_assert(CheckCountOneFromRight<unsigned long>(), "Failed countr_one for unsigned long.");
  static_assert(CheckCountOneFromRight<unsigned long long>(),
                "Failed countr_one for unsigned long long.");
  static_assert(CheckCountOneFromRight<unsigned char>(), "Failed countr_one for unsigned char.");

  static_assert(CheckCountOneFromRight<uint8_t>(), "Failed countr_one for uint8_t.");
  static_assert(CheckCountOneFromRight<uint16_t>(), "Failed countr_one for uint16_t.");
  static_assert(CheckCountOneFromRight<uint32_t>(), "Failed countr_one for uint32_t.");
  static_assert(CheckCountOneFromRight<uint64_t>(), "Failed countr_one for uint64_t.");
  static_assert(CheckCountOneFromRight<uint128_t>(), "Failed countr_one for uint64_t.");

  static_assert(CheckCountOneFromRight<uint_least8_t>(), "Failed countr_one for uint_least8_t.");
  static_assert(CheckCountOneFromRight<uint_least16_t>(), "Failed countr_one for uint_least16_t.");
  static_assert(CheckCountOneFromRight<uint_least32_t>(), "Failed countr_one for uint_least32_t.");
  static_assert(CheckCountOneFromRight<uint_least64_t>(), "Failed countr_one for uint_least64_t.");

  static_assert(CheckCountOneFromRight<uint_fast8_t>(), "Failed countr_one for uint_fast8_t.");
  static_assert(CheckCountOneFromRight<uint_fast16_t>(), "Failed countr_one for uint_fast16_t.");
  static_assert(CheckCountOneFromRight<uint_fast32_t>(), "Failed countr_one for uint_fast32_t.");
  static_assert(CheckCountOneFromRight<uint_fast64_t>(), "Failed countr_one for uint_fast64_t.");
}

template <typename T>
constexpr void CheckRotationLeft() {
  constexpr T kDigits = std::numeric_limits<T>::digits;
  static_assert(cpp20::rotl<T>(8, 0) == 8, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(8, kDigits) == 8, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(8, 2 * kDigits) == 8, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(8, 10000000 * kDigits) == 8, "Rotation Left failed.");

  static_assert(cpp20::rotl<T>(1, 1) == 2, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(2, 1) == 4, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(4, 1) == 8, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(1, 2) == 4, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(2, 2) == 8, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(4, 2) == 16, "Rotation Left failed.");

  static_assert(cpp20::rotl<T>(2, -1) == 1, "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(4, -1) == 2, "Rotation Left failed.");
  static_assert(
      cpp20::rotl<T>(1, -2) == (static_cast<T>(1) << (std::numeric_limits<T>::digits - 2)),
      "Rotation Left failed.");
  static_assert(
      cpp20::rotl<T>(2, -2) == (static_cast<T>(1) << (std::numeric_limits<T>::digits - 1)),
      "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(4, -2) == 1, "Rotation Left failed.");

  // Most significant bit is 0.
  constexpr T kVal = static_cast<T>(~(static_cast<T>(1) << (std::numeric_limits<T>::digits - 1)));
  static_assert(cpp20::rotl<T>(kVal, 1) == static_cast<T>(~1), "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(kVal, -1) ==
                    static_cast<T>(~(static_cast<T>(1) << (std::numeric_limits<T>::digits - 2))),
                "Rotation Left failed.");
  static_assert(cpp20::rotl<T>(kVal, -2) ==
                    static_cast<T>(~(static_cast<T>(1) << (std::numeric_limits<T>::digits - 3))),
                "Rotation Left failed.");
}

TEST(BitOpsTest, RotLWithShiftIsOk) {
  CheckRotationLeft<unsigned>();
  CheckRotationLeft<unsigned char>();
  CheckRotationLeft<unsigned long>();
  CheckRotationLeft<unsigned long long>();

  CheckRotationLeft<uint8_t>();
  CheckRotationLeft<uint16_t>();
  CheckRotationLeft<uint32_t>();
  CheckRotationLeft<uint64_t>();
  CheckRotationLeft<uint128_t>();

  CheckRotationLeft<uint_least8_t>();
  CheckRotationLeft<uint_least16_t>();
  CheckRotationLeft<uint_least32_t>();
  CheckRotationLeft<uint_least64_t>();

  CheckRotationLeft<uint_fast8_t>();
  CheckRotationLeft<uint_fast16_t>();
  CheckRotationLeft<uint_fast32_t>();
  CheckRotationLeft<uint_fast64_t>();
}

template <typename T>
constexpr void CheckRotationRight() {
  constexpr T kDigits = std::numeric_limits<T>::digits;
  static_assert(cpp20::rotr<T>(8, 0) == 8, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(8, kDigits) == 8, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(8, 2 * kDigits) == 8, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(8, 10000000 * kDigits) == 8, "Rotation Right failed.");

  static_assert(cpp20::rotr<T>(1, 1) == (static_cast<T>(1) << (std::numeric_limits<T>::digits - 1)),
                "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(2, 1) == 1, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(4, 1) == 2, "Rotation Right failed.");

  static_assert(cpp20::rotr<T>(1, 2) == (static_cast<T>(1) << (std::numeric_limits<T>::digits - 2)),
                "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(2, 2) == (static_cast<T>(1) << (std::numeric_limits<T>::digits - 1)),
                "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(4, 2) == 1, "Rotation Right failed.");

  static_assert(cpp20::rotr<T>(1, -2) == 4, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(2, -2) == 8, "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(4, -2) == 16, "Rotation Right failed.");

  // Least significant bit is 0.
  constexpr T kVal = static_cast<T>(~1);
  static_assert(cpp20::rotr<T>(kVal, 1) ==
                    static_cast<T>(~(static_cast<T>(1) << std::numeric_limits<T>::digits - 1)),
                "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(kVal, -1) == static_cast<T>(~(static_cast<T>(1) << 1)),
                "Rotation Right failed.");
  static_assert(cpp20::rotr<T>(kVal, -2) == static_cast<T>(~(static_cast<T>(1) << 2)),
                "Rotation Right failed.");
}

TEST(BitOpsTest, RotRWithShiftIsOk) {
  CheckRotationRight<unsigned>();
  CheckRotationRight<unsigned char>();
  CheckRotationRight<unsigned long>();
  CheckRotationRight<unsigned long long>();

  CheckRotationRight<uint8_t>();
  CheckRotationRight<uint16_t>();
  CheckRotationRight<uint32_t>();
  CheckRotationRight<uint64_t>();
  CheckRotationRight<uint128_t>();

  CheckRotationRight<uint_least8_t>();
  CheckRotationRight<uint_least16_t>();
  CheckRotationRight<uint_least32_t>();
  CheckRotationRight<uint_least64_t>();

  CheckRotationRight<uint_fast8_t>();
  CheckRotationRight<uint_fast16_t>();
  CheckRotationRight<uint_fast32_t>();
  CheckRotationRight<uint_fast64_t>();
}

template <typename T>
constexpr bool CheckPopCount() {
  constexpr auto digits = std::numeric_limits<T>::digits;

  // Check for the first i bits set.
  for (auto i = 0; i < digits; ++i) {
    T kVal = (static_cast<T>(1) << i) - 1;
    // Also check powers of 2.
    if (cpp20::popcount(static_cast<T>(static_cast<T>(1) << i)) != 1) {
      return false;
    }
    if (cpp20::popcount(kVal) != i) {
      return false;
    }
  }

  // Scrambled.
  static_assert(cpp20::popcount(static_cast<T>(5)) == 2, "popcount failed.");
  static_assert(cpp20::popcount(static_cast<T>(6)) == 2, "popcount failed.");
  static_assert(cpp20::popcount(static_cast<T>(125)) == 6, "popcount failed.");
  static_assert(cpp20::popcount(static_cast<T>(255)) == 8, "popcount failed.");

  // |max| should have all bits set.
  static_assert(cpp20::popcount(std::numeric_limits<T>::max()) == std::numeric_limits<T>::digits,
                "popcount failed.");

  return true;
}

TEST(BitOpsTest, PopCountIsOk) {
  static_assert(CheckPopCount<unsigned>(), "cpp20::popcount check failed for unsigned.");
  static_assert(CheckPopCount<unsigned char>(), "cpp20::popcount check failed for unsigned char.");
  static_assert(CheckPopCount<unsigned long>(), "cpp20::popcount check failed for unsigned long.");
  static_assert(CheckPopCount<unsigned long long>(),
                "cpp20::popcount check failed for unsigned long long.");

  static_assert(CheckPopCount<uint8_t>(), "cpp20::popcount check failed for uint8_t.");
  static_assert(CheckPopCount<uint16_t>(), "cpp20::popcount check failed for uint16_t.");
  static_assert(CheckPopCount<uint32_t>(), "cpp20::popcount check failed for uint32_t.");
  static_assert(CheckPopCount<uint64_t>(), "cpp20::popcount check failed for uint64_t.");
  static_assert(CheckPopCount<uint128_t>(), "cpp20::popcount check failed for uint64_t.");

  static_assert(CheckPopCount<uint_least8_t>(), "cpp20::popcount check failed for uint_least8_t.");
  static_assert(CheckPopCount<uint_least16_t>(),
                "cpp20::popcount check failed for uint_least16_t.");
  static_assert(CheckPopCount<uint_least32_t>(),
                "cpp20::popcount check failed for uint_least32_t.");
  static_assert(CheckPopCount<uint_least64_t>(),
                "cpp20::popcount check failed for uint_least64_t.");

  static_assert(CheckPopCount<uint_fast8_t>(), "cpp20::popcount check failed for uint_fast8_t.");
  static_assert(CheckPopCount<uint_fast16_t>(), "cpp20::popcount check failed for uint_fast16_t.");
  static_assert(CheckPopCount<uint_fast32_t>(), "cpp20::popcount check failed for uint_fast32_t.");
  static_assert(CheckPopCount<uint_fast64_t>(), "cpp20::popcount check failed for uint_fast64_t.");
}

#if __cpp_lib_bitops >= 201907L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename T>
constexpr void CheckAlias() {
  static_assert(&cpp20::countl_zero<T> == &std::countl_zero<T>, "countl_zero must be alias.");
  static_assert(&cpp20::countl_one<T> == &std::countl_one<T>, "countl_one must be alias.");
  static_assert(&cpp20::countr_zero<T> == &std::countr_zero<T>, "countr_zero must be alias.");
  static_assert(&cpp20::countr_one<T> == &std::countr_one<T>, "countr_one must be alias.");
  static_assert(&cpp20::popcount<T> == &std::popcount<T>, "popcount must be alias.");
  static_assert(&cpp20::rotl<T> == &std::rotl<T>, "rotl must be alias.");
  static_assert(&cpp20::rotr<T> == &std::rotr<T>, "rotr must be alias.");
}

TEST(BitOpsTest, AliasForStdWhenAvailable) {
  CheckAlias<unsigned>();
  CheckAlias<unsigned char>();
  CheckAlias<unsigned long>();
  CheckAlias<unsigned long long>();

  CheckAlias<uint8_t>();
  CheckAlias<uint16_t>();
  CheckAlias<uint32_t>();
  CheckAlias<uint64_t>();
  CheckAlias<uint128_t>();

  CheckAlias<uint_least8_t>();
  CheckAlias<uint_least16_t>();
  CheckAlias<uint_least32_t>();
  CheckAlias<uint_least64_t>();

  CheckAlias<uint_fast8_t>();
  CheckAlias<uint_fast16_t>();
  CheckAlias<uint_fast32_t>();
  CheckAlias<uint_fast64_t>();
}

#endif

}  // namespace
