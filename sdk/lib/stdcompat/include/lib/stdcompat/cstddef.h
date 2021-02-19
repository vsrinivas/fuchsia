// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_CSTDDEF_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_CSTDDEF_H_

#include <cstddef>
#include <type_traits>

#include "version.h"

namespace cpp17 {

#if __cpp_lib_byte >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::byte;
using std::operator<<=;
using std::operator<<;
using std::operator>>=;
using std::operator>>;
using std::operator|=;
using std::operator|;
using std::operator&=;
using std::operator&;
using std::operator^=;
using std::operator^;
using std::operator~;
using std::to_integer;

#else  // Provide polyfill for byte and related functions.

enum class byte : unsigned char {};

template <class IntegerType>
constexpr std::enable_if_t<std::is_integral<IntegerType>::value, byte> operator>>(
    byte b, IntegerType shift) noexcept {
  return static_cast<cpp17::byte>(static_cast<unsigned char>(b) >> shift);
}

template <class IntegerType>
constexpr std::enable_if_t<std::is_integral<IntegerType>::value, byte&> operator>>=(
    byte& b, IntegerType shift) noexcept {
  return b = b >> shift;
}

template <class IntegerType>
constexpr std::enable_if_t<std::is_integral<IntegerType>::value, byte> operator<<(
    byte b, IntegerType shift) noexcept {
  return static_cast<cpp17::byte>(static_cast<unsigned char>(b) << shift);
}

template <class IntegerType>
constexpr std::enable_if_t<std::is_integral<IntegerType>::value, byte&> operator<<=(
    byte& b, IntegerType shift) noexcept {
  return b = b << shift;
}

constexpr byte operator|(byte l, byte r) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(l) | static_cast<unsigned char>(r));
}

constexpr byte& operator|=(byte& l, byte r) noexcept { return l = l | r; }

constexpr byte operator&(byte l, byte r) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(l) & static_cast<unsigned char>(r));
}

constexpr byte& operator&=(byte& l, byte r) noexcept { return l = l & r; }

constexpr byte operator^(byte l, byte r) noexcept {
  return static_cast<byte>(static_cast<unsigned char>(l) ^ static_cast<unsigned char>(r));
}

constexpr byte& operator^=(byte& l, byte r) noexcept { return l = l ^ r; }

constexpr byte operator~(byte b) noexcept {
  return static_cast<byte>(~static_cast<unsigned char>(b));
}

template <typename IntegerType>
constexpr std::enable_if_t<std::is_integral<IntegerType>::value, IntegerType> to_integer(
    byte b) noexcept {
  return static_cast<IntegerType>(b);
}

#endif  // __cpp_lib_byte >= 201603L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_CSTDDEF_H_
