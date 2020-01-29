// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_HARD_INT_H_
#define FBL_HARD_INT_H_

#include <type_traits>

// HardInt is a a strongly-typed wrapper around integral types; HardInts
// do not implicitly convert even where the underlying integral type would.
// HardInt provides equality and comparison operators but does not provide
// arithmetic operators.
//
// HardInt is the same size as the underlying integral type.

// Example:
//     DEFINE_HARD_INT_TYPE(Celsius, uint64_t);
//     DEFINE_HARD_INT_TYPE(Fahrenheit, uint64_t);
//     ...
//     Celsius c1(40);
//     Celsius c2(100);
//     Fahrenheit f(451);
//     c1 + 1;                          <-- Compile error
//     static_assert(c1 == 1);          <-- Compile error
//     assert(c1 == c2);                <-- Allowed
//     static_assert(c1 != c2);         <-- Allowed
//     assert(c1.value() == 40);        <-- Allowed
//     c1 = c2;                         <-- Allowed
//     f = c1;                          <-- Compile error

// Create a wrapper around |base_type| named |type_name|.
#define DEFINE_HARD_INT(type_name, base_type) \
  struct type_name##_type_tag_ {}; \
  using type_name = fbl::HardInt<type_name##_type_tag_, base_type>; \
  \
  static_assert(sizeof(type_name) == sizeof(base_type));

namespace fbl {

template <typename UniqueTagType, typename T>
class HardInt {
public:
  constexpr HardInt() = default;
  constexpr explicit HardInt(T value) : value_(value) {}
  constexpr T value() const { return value_; }

  constexpr bool operator==(const HardInt& other) const {
    return value_ == other.value_;
  }
  constexpr bool operator<(const HardInt& other) const {
    return value_ < other.value_;
  }

private:
  static_assert(std::is_integral<T>::value, "T must be an integral type.");

  T value_;
};

}  // namespace fbl

#endif  // FBL_HARD_INT_H_
