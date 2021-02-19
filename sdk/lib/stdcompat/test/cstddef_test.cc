// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/stdcompat/cstddef.h>

#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

namespace {

TEST(ByteTest, ByteTypeMatchesSpec) {
  static_assert(std::is_same<std::underlying_type_t<cpp17::byte>, unsigned char>::value, "");
  static_assert(std::is_enum<cpp17::byte>::value, "");
}

TEST(ByteTest, ToIntegerReturnsCorrectValue) {
  constexpr const unsigned char kByteValue = 123;
  constexpr const cpp17::byte kByte = static_cast<cpp17::byte>(kByteValue);

  static_assert(cpp17::to_integer<unsigned char>(kByte) == kByteValue,
                "to_integer<T>(byte) should return the integer equivalent of "
                "static_cast<IntegerType>(cpp17::byte).");
}

constexpr bool shift_left_assign(cpp17::byte b, int shift) {
  unsigned char val = cpp17::to_integer<unsigned char>(b);
  b <<= shift;
  return b == cpp17::byte(val << shift);
}

TEST(ByteTest, ShiftLeftReturnsCorrectValue) {
  constexpr const unsigned char kByteValue = 1;
  constexpr const int kShift = 3;
  constexpr const cpp17::byte kByte = static_cast<cpp17::byte>(kByteValue);
  constexpr const cpp17::byte kShiftedByte = static_cast<cpp17::byte>(kByteValue << kShift);

  static_assert((kByte << kShift) == kShiftedByte,
                "static_cast<cpp17::byte>(val) << shift must be equivalent to "
                "static_cast<cpp17::byte>(val << shift).");
  static_assert(shift_left_assign(kByte, kShift), "");
}

constexpr bool shift_right_assign(cpp17::byte b, int shift) {
  unsigned char val = cpp17::to_integer<unsigned char>(b);
  b >>= shift;
  return b == cpp17::byte(val >> shift);
}

TEST(ByteTest, ShiftRightReturnsCorrectValue) {
  constexpr const int kShift = 3;
  constexpr const unsigned char kByteValue = 1 << kShift;
  constexpr const cpp17::byte kByte = static_cast<cpp17::byte>(kByteValue);
  constexpr const cpp17::byte kShiftedByte = static_cast<cpp17::byte>(kByteValue >> kShift);

  static_assert((kByte >> kShift) == kShiftedByte,
                "static_cast<cpp17::byte>(val) >> shift must be equivalent to "
                "static_cast<cpp17::byte>(val >> shift).");
  static_assert(shift_right_assign(kByte, kShift), "");
}

constexpr bool or_assign(cpp17::byte l, cpp17::byte r) {
  unsigned char val_l = cpp17::to_integer<unsigned char>(l);
  unsigned char val_r = cpp17::to_integer<unsigned char>(r);
  l |= r;
  return l == static_cast<cpp17::byte>(val_l | val_r);
}

TEST(ByteTest, BitwiseOrOperatesOnUnderlyingType) {
  constexpr const unsigned char kByteValue1 = 4;
  constexpr const unsigned char kByteValue2 = 1;
  constexpr const cpp17::byte kByte1 = static_cast<cpp17::byte>(kByteValue1);
  constexpr const cpp17::byte kByte2 = static_cast<cpp17::byte>(kByteValue2);
  constexpr const cpp17::byte kOrByte = static_cast<cpp17::byte>(kByteValue1 | kByteValue2);

  static_assert((kByte1 | kByte2) == kOrByte,
                "static_cast<cpp17::byte>(val) | shift must be equivalent to "
                "static_cast<cpp17::byte>(val | shift).");
  static_assert(or_assign(kByte1, kByte2), "");
}

constexpr bool and_assign(cpp17::byte l, cpp17::byte r) {
  unsigned char val_l = cpp17::to_integer<unsigned char>(l);
  unsigned char val_r = cpp17::to_integer<unsigned char>(r);
  l &= r;
  return l == static_cast<cpp17::byte>(val_l & val_r);
}

TEST(ByteTest, BitwiseAndOperatesOnUnderlyingType) {
  constexpr const unsigned char kByteValue1 = 4;
  constexpr const unsigned char kByteValue2 = 5;
  constexpr const cpp17::byte kByte1 = static_cast<cpp17::byte>(kByteValue1);
  constexpr const cpp17::byte kByte2 = static_cast<cpp17::byte>(kByteValue2);
  constexpr const cpp17::byte kAndByte = static_cast<cpp17::byte>(kByteValue1 & kByteValue2);

  static_assert((kByte1 & kByte2) == kAndByte,
                "static_cast<cpp17::byte>(val) & shift must be equivalent to "
                "static_cast<cpp17::byte>(val & shift).");
  static_assert(and_assign(kByte1, kByte2), "");
}

constexpr bool xor_assign(cpp17::byte l, cpp17::byte r) {
  unsigned char val_l = cpp17::to_integer<unsigned char>(l);
  unsigned char val_r = cpp17::to_integer<unsigned char>(r);
  l ^= r;
  return l == static_cast<cpp17::byte>(val_l ^ val_r);
}

TEST(ByteTest, BitwiseXorOperatesOnUnderlyingType) {
  constexpr const unsigned char kByteValue1 = 4;
  constexpr const unsigned char kByteValue2 = 3;
  constexpr const cpp17::byte kByte1 = static_cast<cpp17::byte>(kByteValue1);
  constexpr const cpp17::byte kByte2 = static_cast<cpp17::byte>(kByteValue2);
  constexpr const cpp17::byte kXorByte = static_cast<cpp17::byte>(kByteValue1 ^ kByteValue2);

  static_assert((kByte1 ^ kByte2) == kXorByte,
                "static_cast<cpp17::byte>(val) ^ shift must be equivalent to "
                "static_cast<cpp17::byte>(val ^ shift).");
  static_assert(xor_assign(kByte1, kByte2), "");
}

TEST(ByteTest, BitwiseNotOperatesOnUnderlyingType) {
  constexpr const unsigned char kByteValue = 1;
  constexpr const cpp17::byte kByte = static_cast<cpp17::byte>(kByteValue);
  constexpr const cpp17::byte kNotByte = static_cast<cpp17::byte>(~kByteValue);

  static_assert(
      (~kByte) == kNotByte,
      "~static_cast<cpp17::byte>(val) must be equivalent to static_cast<cpp17::byte>(~val).");
}

#if ___cpp_lib_byte >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(ByteTest, IsAliasForStdWhenAvailable) {
  static_assert(std::is_same<std::byte, cpp17::byte>::value, "");
  static_assert(&std::to_integer<unsigned char> == &cpp17::to_integer<unsigned char>, "");

  constexpr cpp17::byte& (*cpp17_byte_shift_left_assign)(cpp17::byte&, unsigned) =
      &cpp17::operator<<=;
  constexpr std::byte& (*std_byte_shift_left_assign)(std::byte&, unsigned) = &std::operator<<=;
  static_assert(cpp17_byte_shift_left_assign == std_byte_shift_left_assign, "");

  constexpr cpp17::byte (*cpp17_byte_shift_left)(cpp17::byte, unsigned) = &cpp17::operator<<;
  constexpr std::byte (*std_byte_shift_left)(std::byte, unsigned) = &std::operator<<;
  static_assert(cpp17_byte_shift_left == std_byte_shift_left, "");

  constexpr cpp17::byte& (*cpp17_byte_shift_right_assign)(cpp17::byte&, unsigned) =
      &cpp17::operator>>=;
  constexpr std::byte& (*std_byte_shift_right_assign)(std::byte&, unsigned) = &std::operator>>=;
  static_assert(cpp17_byte_shift_right_assign == std_byte_shift_right_assign, "");

  constexpr cpp17::byte (*cpp17_byte_shift_right)(cpp17::byte, unsigned) = &cpp17::operator>>;
  constexpr std::byte (*std_byte_shift_right)(std::byte, unsigned) = &std::operator>>;
  static_assert(cpp17_byte_shift_right == std_byte_shift_right, "");

  constexpr cpp17::byte& (*cpp17_byte_or_assign)(cpp17::byte&, cpp17::byte) = &cpp17::operator|=;
  constexpr std::byte& (*std_byte_or_assign)(std::byte&, std::byte) = &std::operator|=;
  static_assert(cpp17_byte_or_assign == std_byte_or_assign, "");

  constexpr cpp17::byte (*cpp17_byte_or)(cpp17::byte, cpp17::byte) = &cpp17::operator|;
  constexpr std::byte (*std_byte_or)(std::byte, cpp17::byte) = &std::operator|;
  static_assert(cpp17_byte_or == std_byte_or, "");

  constexpr cpp17::byte& (*cpp17_byte_and_assign)(cpp17::byte&, cpp17::byte) = &cpp17::operator&=;
  constexpr std::byte& (*std_byte_and_assign)(std::byte&, std::byte) = &std::operator&=;
  static_assert(cpp17_byte_and_assign == std_byte_and_assign, "");

  constexpr cpp17::byte (*cpp17_byte_and)(cpp17::byte, cpp17::byte) = &cpp17::operator&;
  constexpr std::byte (*std_byte_and)(std::byte, cpp17::byte) = &std::operator&;
  static_assert(cpp17_byte_and == std_byte_and, "");

  constexpr cpp17::byte& (*cpp17_byte_xor_assign)(cpp17::byte&, cpp17::byte) = &cpp17::operator^=;
  constexpr std::byte& (*std_byte_xor_assign)(std::byte&, std::byte) = &std::operator^=;
  static_assert(cpp17_byte_xor_assign == std_byte_xor_assign, "");

  constexpr cpp17::byte (*cpp17_byte_xor)(cpp17::byte, cpp17::byte) = &cpp17::operator^;
  constexpr std::byte (*std_byte_xor)(std::byte, cpp17::byte) = &std::operator^;
  static_assert(cpp17_byte_xor == std_byte_xor, "");

  constexpr cpp17::byte (*cpp17_byte_not)(cpp17::byte) = &cpp17::operator~;
  constexpr std::byte (*std_byte_not)(std::byte) = &std::operator~;
  static_assert(cpp17_byte_not == std_byte_not, "");
}

#endif  // ___cpp_lib_byte >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS

}  // namespace
