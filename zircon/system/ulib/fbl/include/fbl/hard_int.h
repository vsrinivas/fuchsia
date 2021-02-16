// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_HARD_INT_H_
#define FBL_HARD_INT_H_

#include <type_traits>

// HardInt is a strongly-typed wrapper around integral types. HardInts do not
// implicitly convert even where the underlying integral type would. HardInt
// provides simple equality and a less-than operator (so the type can be used
// with containers such as std::map or sorted), but does not provide other
// comparison or arithmetic operations.
//
// HardInt types are appropriate for opaque identifiers, such as database IDs,
// resource IDs, etc. If arithmetic needs to be performed, consider instead
// using the StrongInt type in <fbl/strong_int.h>.
//
// HardInt is the same size as the underlying integral type.

// Example:
//     DEFINE_HARD_INT(DogId, uint64_t)
//     DEFINE_HARD_INT(CatId, uint64_t)
//     ...
//     DogId dog1(40);
//     DogId dog2(100);
//     CatId cat(451);
//     dog1 + 1;                      <-- Compile error
//     dog1 + dog2;                   <-- Compile error
//     static_assert(dog1 == 1);      <-- Compile error
//     assert(dog1 == dog2);          <-- Allowed
//     static_assert(dog1 != dog2);   <-- Allowed
//     assert(dog1.value() == 40);    <-- Allowed
//     dog1 = dog2;                   <-- Allowed
//     cat = dog1;                    <-- Compile error

// Create a wrapper around |base_type| named |type_name|.
#define DEFINE_HARD_INT(type_name, base_type)                       \
  struct type_name##_type_tag_ {};                                  \
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

  constexpr bool operator==(const HardInt& other) const { return value_ == other.value_; }
  constexpr bool operator<(const HardInt& other) const { return value_ < other.value_; }

 private:
  static_assert(std::is_integral<T>::value, "T must be an integral type.");

  T value_;
};

}  // namespace fbl

#endif  // FBL_HARD_INT_H_
