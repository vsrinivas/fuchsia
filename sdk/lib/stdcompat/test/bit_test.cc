// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/bit.h>

#include <array>
#include <limits>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

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

}  // namespace
