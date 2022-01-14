// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FIELD_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FIELD_H_

#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/type_traits.h>
#include <lib/stdcompat/utility.h>

#include <cstdint>

namespace elfldltl {

// This wraps an unsigned integer type T, which might need byte-swapping.
// If kSwap is false, this is a fancy way to just define a plain integer.
// If kSwap is true, then assignments, extractions, and comparisons (only
// == and != are supported, not all inequalities) perform byte-swapping.
// The type is constexpr friendly and safely default (zero) constructible.
// But it's usually used only via a pointer to memory holding data from an
// ELF file or target process memory.
template <typename T, bool kSwap>
class UnsignedField {
 public:
  using value_type = T;

  static_assert(std::is_integral_v<value_type>);
  static_assert(std::is_unsigned_v<value_type>);

  constexpr UnsignedField() = default;

  constexpr UnsignedField(const UnsignedField&) = default;

  constexpr UnsignedField(value_type x) : value_(Convert(x)) {}

  explicit constexpr UnsignedField(std::array<uint8_t, sizeof(value_type)> bytes)
      : value_(Convert(bytes)) {}

  constexpr UnsignedField& operator=(const UnsignedField&) = default;

  constexpr UnsignedField& operator=(value_type x) {
    value_ = Convert(x);
    return *this;
  }

  constexpr value_type get() const { return Convert(value_); }

  constexpr value_type operator()() const { return get(); }

  constexpr operator value_type() const { return get(); }

 private:
  static constexpr value_type Convert(std::array<uint8_t, sizeof(value_type)> bytes) {
    auto [first, last] = [&bytes]() {
      if constexpr (cpp20::endian::native == cpp20::endian::little) {
        return std::make_pair(bytes.crbegin(), bytes.crend());
      } else if constexpr (cpp20::endian::native == cpp20::endian::big) {
        return std::make_pair(bytes.cbegin(), bytes.cend());
      }
    }();
    value_type x{};
    for (auto it = first; it != last; ++it) {
      x <<= 8;
      x |= *it;
    }
    return x;
  }

  static constexpr value_type Convert(value_type val) {
    if constexpr (kSwap) {
      // These are portable expressions for byte-swapping but the compiler will
      // recognize the pattern and emit the optimal single instruction or two.
      if constexpr (sizeof(value_type) == sizeof(uint64_t)) {
        val = (((val >> 56) & 0xff) << 0) | (((val >> 48) & 0xff) << 8) |
              (((val >> 40) & 0xff) << 16) | (((val >> 32) & 0xff) << 24) |
              (((val >> 24) & 0xff) << 32) | (((val >> 16) & 0xff) << 40) |
              (((val >> 8) & 0xff) << 48) | (((val >> 0) & 0xff) << 56);
      } else if constexpr (sizeof(value_type) == sizeof(uint32_t)) {
        val = (((val >> 24) & 0xff) << 0) | (((val >> 16) & 0xff) << 8) |
              (((val >> 8) & 0xff) << 16) | (((val >> 0) & 0xff) << 24);
      } else if constexpr (sizeof(value_type) == sizeof(uint16_t)) {
        val = static_cast<value_type>(((val >> 8) & 0xff) << 0) |
              static_cast<value_type>(((val >> 0) & 0xff) << 8);
      } else {
        static_assert(sizeof(value_type) == sizeof(uint8_t));
      }
    }
    return val;
  }

  value_type value_{};
};

// This is like UnsignedField but for signed integer types.
// Note that T is the corresponding unsigned integer type, not
// the signed integer type.  The SignedField<T> object behaves
// for implicit conversions like the signed integer type.
template <typename T, bool kSwap>
class SignedField final : public UnsignedField<T, kSwap> {
 public:
  using Base = UnsignedField<T, kSwap>;

  using value_type = std::make_signed_t<T>;

  using Base::Base;

  constexpr SignedField& operator=(value_type x) {
    Base::operator=(cpp20::bit_cast<T>(x));
    return *this;
  }

  constexpr value_type get() const { return Base::get(); }

  constexpr value_type operator()() const { return get(); }

  constexpr operator value_type() const { return get(); }
};

// This is like UnsignedField but for enum types defined with a specified
// underlying unsigned integer type.  The underlying type of the actual field
// to access (before possible byte-swapping) can be given as an explicit
// template argument if in case it differs from the enum's underlying type.
template <typename T, bool kSwap, typename Uint = std::underlying_type_t<T>>
class EnumField final {
 public:
  using value_type = T;
  static_assert(std::is_enum_v<value_type>);

  constexpr EnumField() = default;

  constexpr EnumField(const EnumField&) = default;

  constexpr EnumField(value_type x) : value_(static_cast<Uint>(x)) {}

  constexpr EnumField& operator=(const EnumField&) = default;

  constexpr EnumField& operator=(value_type x) {
    value_ = static_cast<Uint>(x);
    return *this;
  }

  constexpr bool operator==(const EnumField& other) { return value_ == other.value_; }
  constexpr bool operator!=(const EnumField& other) { return value_ != other.value_; }

  constexpr bool operator==(value_type other) { return *this == EnumField{other}; }
  constexpr bool operator!=(value_type other) { return *this != EnumField{other}; }

  constexpr value_type get() const { return static_cast<value_type>(value_.get()); }

  constexpr value_type operator()() const { return get(); }

  constexpr operator value_type() const { return get(); }

 private:
  UnsignedField<Uint, kSwap> value_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_FIELD_H_
