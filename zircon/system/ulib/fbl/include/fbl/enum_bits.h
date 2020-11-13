// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ENUM_BITS_H_
#define FBL_ENUM_BITS_H_

#include <zircon/compiler.h>

#include <type_traits>

// Utility to make using enum classes as bit fields more ergonomic.
//
// This utility supplies overloads of the bitwise operators that bind only to
// enum types tagged with the macro FBL_ENABLE_ENUM_BITS(type). The macro must
// be called in the same namespace as the enum type being tagged.
//
// The operators only accept values of the same type. Mixing types requres
// casting.
//
// Examples:
//
// enum class Bits : uint64_t {
//   FieldA = 0b0001,
//   FieldB = 0b0010,
//   Mask   = FieldA | FieldB,
// };
// FBL_ENABLE_ENUM_BITS(Bits)
//
//
// namespace foo {
//   enum class Bits : uint32_t {
//     // ...
//   };
//   FBL_ENABLE_ENUM_BITS(Bits)
// }  // namespace foo
//
// struct Bar {
//   enum class Bits : uint8_t {
//     // ...
//   };
//   // ...
// };
// FBL_ENABLE_ENUM_BITS(Bar::Bits)
//

// Tags the given enum for use as a bit field, enabling the overloaded bitwise
// operators. Must be called in the same scope that |type| is defined in.
#define FBL_ENABLE_ENUM_BITS(type)                                          \
  static_assert(std::is_enum_v<type>, "Type must be an enum: type=" #type); \
  [[maybe_unused]] static constexpr bool FBL__EnumBitsTag(const type&) { return true; }

namespace fbl::internal {

// Looks for a function defined in the same namespace as T with the signature
// bool FBL__EnumBitsTag(const T&) via ADL.
template <typename T>
using EnumBitsTag = decltype(FBL__EnumBitsTag(std::declval<const T>()));

// Evaluates to true if type T is tagged
template <typename, typename = void>
struct IsEnumBits : std::false_type {};
template <typename T>
struct IsEnumBits<T, std::void_t<EnumBitsTag<T>>> : std::true_type {};

// Enable if T is properly a properly tagged enum. Double checks that T is an
// enum to as defense in depth.
template <typename T>
using EnableIfEnumBits = std::enable_if_t<std::is_enum_v<T> && IsEnumBits<T>::value>;

}  // namespace fbl::internal

// Operator overloads at global scope enabled only for types tagged as enum
// fields.
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T operator|(T a, T b) {
  using U = typename std::underlying_type<T>::type;
  return static_cast<T>(static_cast<U>(a) | static_cast<U>(b));
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T operator&(T a, T b) {
  using U = typename std::underlying_type<T>::type;
  return static_cast<T>(static_cast<U>(a) & static_cast<U>(b));
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T operator^(T a, T b) {
  using U = typename std::underlying_type<T>::type;
  return static_cast<T>(static_cast<U>(a) ^ static_cast<U>(b));
}

template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T& operator|=(T& a, T b) {
  a = a | b;
  return a;
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T& operator&=(T& a, T b) {
  a = a & b;
  return a;
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T& operator^=(T& a, T b) {
  a = a ^ b;
  return a;
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr T operator~(T value) {
  using U = typename std::underlying_type<T>::type;
  return static_cast<T>(~static_cast<U>(value));
}
template <typename T, typename = ::fbl::internal::EnableIfEnumBits<T>>
constexpr bool operator!(T value) {
  using U = typename std::underlying_type<T>::type;
  return !static_cast<U>(value);
}

#endif  // FBL_ENUM_BITS_H_
