// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <fbl/enum_bits.h>
#include <zxtest/zxtest.h>

namespace test {

enum class NonBits {};
static_assert(!::fbl::internal::IsEnumBits<NonBits>::value);

enum class Bits : uint64_t {
  None = 0b00,
  A = 0b01,
  B = 0b10,
  C = A | B,
};
FBL_ENABLE_ENUM_BITS(Bits)
static_assert(::fbl::internal::IsEnumBits<Bits>::value);

struct Nested {
  enum class Bits : uint64_t {
    None = 0b00,
    A = 0b01,
    B = 0b10,
    C = A | B,
  };
};
FBL_ENABLE_ENUM_BITS(Nested::Bits)
static_assert(::fbl::internal::IsEnumBits<Nested::Bits>::value);

template <typename B>
constexpr bool TestOperators() {
  static_assert((B::A | B::A) == B::A);
  static_assert((B::A | B::B) == B::C);
  static_assert((B::A | B::C) == B::C);
  static_assert((B::B | B::A) == B::C);
  static_assert((B::B | B::B) == B::B);
  static_assert((B::B | B::C) == B::C);
  static_assert((B::C | B::A) == B::C);
  static_assert((B::C | B::B) == B::C);
  static_assert((B::C | B::C) == B::C);

  static_assert((B::A & B::A) == B::A);
  static_assert((B::A & B::B) == B::None);
  static_assert((B::A & B::C) == B::A);
  static_assert((B::B & B::A) == B::None);
  static_assert((B::B & B::B) == B::B);
  static_assert((B::B & B::C) == B::B);
  static_assert((B::C & B::A) == B::A);
  static_assert((B::C & B::B) == B::B);
  static_assert((B::C & B::C) == B::C);

  static_assert((B::A ^ B::A) == B::None);
  static_assert((B::A ^ B::B) == B::C);
  static_assert((B::A ^ B::C) == B::B);
  static_assert((B::B ^ B::A) == B::C);
  static_assert((B::B ^ B::B) == B::None);
  static_assert((B::B ^ B::C) == B::A);
  static_assert((B::C ^ B::A) == B::B);
  static_assert((B::C ^ B::B) == B::A);
  static_assert((B::C ^ B::C) == B::None);

  static_assert(~B::C != B::None);
  static_assert((B::C & ~B::A) == B::B);

  static_assert(!B::None == true);
  static_assert(!B::C == false);

  // Only verifying that compound assignment operators compile in constexpr
  // context.
  B bits = B::A;
  bits &= B::C;
  bits ^= B::C;
  bits |= B::B;

  return true;
}

static_assert(TestOperators<Bits>());
static_assert(TestOperators<Nested::Bits>());

#if 0 || TEST_DOES_NOT_COMPILE
static_assert(TestOperators<NonBits>());
#endif

}  // namespace test

TEST(EnumBits, Operators) {
  using test::Bits;
  using test::Nested;

  EXPECT_EQ((Bits::A | Bits::A), Bits::A);
  EXPECT_EQ((Bits::A | Bits::B), Bits::C);
  EXPECT_EQ((Bits::A | Bits::C), Bits::C);
  EXPECT_EQ((Bits::B | Bits::A), Bits::C);
  EXPECT_EQ((Bits::B | Bits::B), Bits::B);
  EXPECT_EQ((Bits::B | Bits::C), Bits::C);
  EXPECT_EQ((Bits::C | Bits::A), Bits::C);
  EXPECT_EQ((Bits::C | Bits::B), Bits::C);
  EXPECT_EQ((Bits::C | Bits::C), Bits::C);

  EXPECT_EQ((Bits::A & Bits::A), Bits::A);
  EXPECT_EQ((Bits::A & Bits::B), Bits::None);
  EXPECT_EQ((Bits::A & Bits::C), Bits::A);
  EXPECT_EQ((Bits::B & Bits::A), Bits::None);
  EXPECT_EQ((Bits::B & Bits::B), Bits::B);
  EXPECT_EQ((Bits::B & Bits::C), Bits::B);
  EXPECT_EQ((Bits::C & Bits::A), Bits::A);
  EXPECT_EQ((Bits::C & Bits::B), Bits::B);
  EXPECT_EQ((Bits::C & Bits::C), Bits::C);

  EXPECT_EQ((Bits::A ^ Bits::A), Bits::None);
  EXPECT_EQ((Bits::A ^ Bits::B), Bits::C);
  EXPECT_EQ((Bits::A ^ Bits::C), Bits::B);
  EXPECT_EQ((Bits::B ^ Bits::A), Bits::C);
  EXPECT_EQ((Bits::B ^ Bits::B), Bits::None);
  EXPECT_EQ((Bits::B ^ Bits::C), Bits::A);
  EXPECT_EQ((Bits::C ^ Bits::A), Bits::B);
  EXPECT_EQ((Bits::C ^ Bits::B), Bits::A);
  EXPECT_EQ((Bits::C ^ Bits::C), Bits::None);

  EXPECT_NE(~Bits::C, Bits::None);
  EXPECT_EQ((Bits::C & ~Bits::A), Bits::B);

  EXPECT_TRUE(!Bits::None);
  EXPECT_FALSE(!Bits::C);

  {
    Bits value;
    value = Bits::A;
    value &= Bits::C;
    EXPECT_EQ(Bits::A, value);

    value = Bits::A;
    value ^= Bits::C;
    EXPECT_EQ(Bits::B, value);

    value = Bits::A;
    value |= Bits::B;
    EXPECT_EQ(Bits::C, value);

    EXPECT_TRUE(!Bits::None);
    EXPECT_FALSE(!Bits::A);
  }

  EXPECT_EQ((Nested::Bits::A | Nested::Bits::A), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::A | Nested::Bits::B), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::A | Nested::Bits::C), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::B | Nested::Bits::A), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::B | Nested::Bits::B), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::B | Nested::Bits::C), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::C | Nested::Bits::A), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::C | Nested::Bits::B), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::C | Nested::Bits::C), Nested::Bits::C);

  EXPECT_EQ((Nested::Bits::A & Nested::Bits::A), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::A & Nested::Bits::B), Nested::Bits::None);
  EXPECT_EQ((Nested::Bits::A & Nested::Bits::C), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::B & Nested::Bits::A), Nested::Bits::None);
  EXPECT_EQ((Nested::Bits::B & Nested::Bits::B), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::B & Nested::Bits::C), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::C & Nested::Bits::A), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::C & Nested::Bits::B), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::C & Nested::Bits::C), Nested::Bits::C);

  EXPECT_EQ((Nested::Bits::A ^ Nested::Bits::A), Nested::Bits::None);
  EXPECT_EQ((Nested::Bits::A ^ Nested::Bits::B), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::A ^ Nested::Bits::C), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::B ^ Nested::Bits::A), Nested::Bits::C);
  EXPECT_EQ((Nested::Bits::B ^ Nested::Bits::B), Nested::Bits::None);
  EXPECT_EQ((Nested::Bits::B ^ Nested::Bits::C), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::C ^ Nested::Bits::A), Nested::Bits::B);
  EXPECT_EQ((Nested::Bits::C ^ Nested::Bits::B), Nested::Bits::A);
  EXPECT_EQ((Nested::Bits::C ^ Nested::Bits::C), Nested::Bits::None);

  EXPECT_NE(~Nested::Bits::C, Nested::Bits::None);
  EXPECT_EQ((Nested::Bits::C & ~Nested::Bits::A), Nested::Bits::B);

  EXPECT_TRUE(!Nested::Bits::None);
  EXPECT_FALSE(!Nested::Bits::C);

  {
    Nested::Bits value;
    value = Nested::Bits::A;
    value &= Nested::Bits::C;
    EXPECT_EQ(Nested::Bits::A, value);

    value = Nested::Bits::A;
    value ^= Nested::Bits::C;
    EXPECT_EQ(Nested::Bits::B, value);

    value = Nested::Bits::A;
    value |= Nested::Bits::B;
    EXPECT_EQ(Nested::Bits::C, value);

    EXPECT_TRUE(!Nested::Bits::None);
    EXPECT_FALSE(!Nested::Bits::A);
  }
}
