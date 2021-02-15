// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STRONG_INT_H_
#define FBL_STRONG_INT_H_

#include <type_traits>

// StrongInt is a strongly-typed wrapper around integer types that supports
// standard arithmetic operations. Different types of StrongInts do not
// implicitly convert, even where the underlying integral type would.
//
// StrongInt provides the standard set of arithmetic operations, including
// basic arithmetic (such as `x + y`), bitwise operations (such as `x | y`),
// comparison operations (such as `x <= y`), and unary operators (such as
// `-x`, `~x`, `++x`, `x++`).
//
// For opaque integer types where arithmetic is not needed (such as handles or
// other identifiers), consider using the HardInt type in <fbl/hard_int.h>
// instead.
//
// StrongInt types are the same size as the underlying type.
//
// Example:
//     DEFINE_STRONG_INT(CpuCount, uint64_t)
//     DEFINE_STRONG_INT(MemoryBytes, uint64_t)
//     ...
//     CpuCount c1(3);
//     CpuCount c2(5);
//     MemoryBytes m(4096);
//     c1 + 1;                          <-- Compile error: can't implicitly convert 1 to CpuCount
//     c1 + CpuCount(1);                <-- Allowed
//     assert(c1 == 3);                 <-- Compile error: can't implicitly convert 1 to CpuCount
//     assert(c1.value() == 3);         <-- Allowed
//     assert(c1 != c2);                <-- Allowed
//     c1 = c2;                         <-- Allowed
//     m = c1;                          <-- Compile error
//
// Non-scalar arithmetic operations (addition, subtraction, etc) are allowed
// on StrongInt types with other instances of the same StrongInt type:
//
//     c1 + c2;                         <-- Allowed, producing the type CpuCount.
//     c1 += c2;                        <-- Allowed
//     c1++;                            <-- Allowed
//     c1 + m;                          <-- Compile error: mixing CpuCount and MemoryBytes
//     c1 + 1;                          <-- Compile error: mixing CpuCount and plain int.
//
// StrongInt types also supports scalar operations (multiplication, division,
// modulo, and shifting by plain integral types) by plain integral types:
//
//     CpuCount c(1);
//     auto cpu_count = c * 2;          <-- Allowed, producing the type `CpuCount`.
//     c /= 2;                          <-- Allowed
//     c << 2;                          <-- Allowed
//
// Bitwise operators are supported on the generated type, though users must
// ensure that these operations are sensible.
//
// The full set of supported arithmetic operations are as follows:
//
//     StrongInt + StrongInt --> StrongInt
//     StrongInt - StrongInt --> StrongInt
//     StrongInt & StrongInt --> StrongInt
//     StrongInt | StrongInt --> StrongInt
//     StrongInt ^ StrongInt --> StrongInt
//
//     StrongInt * Value --> StrongInt
//     Value * StrongInt --> StrongInt
//
//     StrongInt / Value --> StrongInt
//     StrongInt / StrongInt --> Value
//
//     StrongInt % StrongInt --> StrongInt
//     StrongInt % Value --> StrongInt
//
//     StrongInt << Value --> StrongInt
//     StrongInt >> Value --> StrongInt
//
//     StrongInt { <= | < | == | > | >= | != } StrongInt --> bool
//
//     StrongInt++
//     StrongInt--

// Create a wrapper around |base_type| named |type_name|.
#define DEFINE_STRONG_INT(type_name, base_type)                                  \
  struct type_name##_strong_int_type_tag_ {};                                    \
  using type_name = fbl::StrongInt<type_name##_strong_int_type_tag_, base_type>; \
  static_assert(sizeof(type_name) == sizeof(base_type));

namespace fbl {

template <typename UniqueTagType, typename T>
class StrongInt {
 public:
  constexpr StrongInt() = default;
  constexpr explicit StrongInt(T value) : value_(value) {}
  constexpr T value() const { return value_; }

  // Define comparison operators.
#define FBL_STRONG_INT_COMPARISON_OP(op)                                          \
  friend constexpr bool operator op(const StrongInt& lhs, const StrongInt& rhs) { \
    return lhs.value_ op rhs.value_;                                              \
  }
  FBL_STRONG_INT_COMPARISON_OP(==)
  FBL_STRONG_INT_COMPARISON_OP(!=)
  FBL_STRONG_INT_COMPARISON_OP(<=)
  FBL_STRONG_INT_COMPARISON_OP(>=)
  FBL_STRONG_INT_COMPARISON_OP(<)
  FBL_STRONG_INT_COMPARISON_OP(>)

  // Define binary operators where both the LHS and RHS are of the same StrongInt.
#define FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(op)                                     \
  friend constexpr StrongInt operator op(const StrongInt& lhs, const StrongInt& rhs) { \
    return StrongInt(lhs.value_ op rhs.value_);                                        \
  }                                                                                    \
  constexpr StrongInt& operator op##=(const StrongInt& other) {                        \
    value_ op## = other.value_;                                                        \
    return *this;                                                                      \
  }
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(+)
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(-)
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(&)
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(|)
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(^)
  FBL_STRONG_INT_STRONG_STRONG_BINARY_OP(%)

  // Define binary operations which can take place on a compatible numeric type
  // on the right hand side.
#define FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(op)                            \
  friend constexpr StrongInt operator op(const StrongInt& lhs, const T& rhs) { \
    return StrongInt(lhs.value_ op rhs);                                       \
  }                                                                            \
  constexpr StrongInt& operator op##=(const T& other) {                        \
    value_ op## = other;                                                       \
    return *this;                                                              \
  }
  FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(/)
  FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(*)
  FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(%)
  FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(<<)
  FBL_STRONG_INT_STRONG_NUMERIC_BINARY_OP(>>)

  // <value type> * StrongInt. The symmetrical case is implemented in the macro above.
  friend constexpr StrongInt operator*(const T& lhs, const StrongInt& rhs) {
    return StrongInt(lhs * rhs.value_);
  }

  // StrongInt / StrongInt == value_type.
  friend constexpr T operator/(const StrongInt& lhs, const StrongInt& rhs) {
    return lhs.value_ / rhs.value_;
  }

  // Define unary operations.
  constexpr auto& operator++() {
    ++value_;
    return *this;
  }
  constexpr auto& operator--() {
    --value_;
    return *this;
  }
  constexpr auto operator++(int postfix) { return StrongInt(value_++); }
  constexpr auto operator--(int postfix) { return StrongInt(value_--); }
  constexpr auto operator+() { return StrongInt(+value_); }
  constexpr auto operator-() { return StrongInt(-value_); }
  constexpr auto operator~() { return StrongInt(~value_); }

  // Truth testing.
  explicit operator bool() { return value_ != 0; }

 private:
  static_assert(std::is_integral<T>::value, "T must be an arithmetic type.");
  static_assert(!std::is_same<T, bool>::value, "StrongInt does not support bool.");

  T value_ = 0;
};

}  // namespace fbl

#endif  // FBL_STRONG_INT_H_
