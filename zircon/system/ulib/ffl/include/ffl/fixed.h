// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef FFL_FIXED_H_
#define FFL_FIXED_H_

//
// Fuchsia Fixed-point Library (FFL):
//
// An efficient header-only multi-precision fixed point math library with well-
// defined rounding.
//

#include <cstddef>
#include <type_traits>

#include <ffl/expression.h>
#include <ffl/fixed_format.h>
#include <ffl/saturating_arithmetic.h>
#include <ffl/utility.h>

namespace ffl {

// Represents a fixed-point value using the given integer base type |Integer|
// and the given number of fractional bits |FractionalBits|. This type supports
// standard arithmetic operations and comparisons between the same type, fixed-
// point types with different precision/resolution, and integer values.
//
// Arithmetic operations are not immediately computed. Instead, arithmetic
// expressions involving fixed-point types are assembled into intermediate
// expression trees (via the Expression template type) that capture operands and
// order of operations. The value of the expression tree is evaluated when it is
// assigned to a fixed-point variable. Using this approach the precision and
// resolution of intermediate values are selected at compile time, based on the
// final precision and resolution of the destination variable.
//
// See README.md for a more detailed discussion of fixed-point arithmetic,
// rounding, precision, and resolution in this library.
//
template <typename Integer, size_t FractionalBits>
class Fixed {
 public:
  // Alias of the FixedFormat type describing traits and low-level operations
  // on the fixed-point representation of this type.
  using Format = FixedFormat<Integer, FractionalBits>;

  // Returns the given raw integer as a fixed-point value in this format.
  static constexpr Fixed FromRaw(Integer value) {
    return ValueExpression<Integer, FractionalBits>{value};
  }

  // Returns the minimum value of this fixed point format.
  static constexpr Fixed Min() { return FromRaw(Format::Min); }

  // Returns the maximum value of this fixed point format.
  static constexpr Fixed Max() { return FromRaw(Format::Max); }

  // Fixed is default constructible without a default value, which is the same
  // as for plain integer types. This is permitted in constexpr contexts as
  // long as the underling integer member |value_| is initialized before use.
  constexpr Fixed() = default;

  // Fixed is copy constructible and assignable.
  constexpr Fixed(const Fixed&) = default;
  constexpr Fixed& operator=(const Fixed&) = default;

  // Explicit conversion from an integer value. The value is saturated to fit
  // within the integer precision defined by Format::IntegerBits.
  explicit constexpr Fixed(Integer value) : Fixed{ToExpression<Integer>{value}} {}

  // Implicit conversion from an intermediate expression. The value is converted
  // to the precision and resolution of this type, if necessary.
  template <Operation Op, typename... Args>
  constexpr Fixed(Expression<Op, Args...> expression)
      : Fixed{Format::Convert(expression.Evaluate(Format{}))} {}

  // Explicit conversion from another fixed point type. The value is converted
  // to the precision and resolution of this type, if necessary.
  template <typename OtherInteger, size_t OtherFractionalBits,
            typename = std::enable_if_t<!std::is_same_v<Integer, OtherInteger> ||
                                        FractionalBits != OtherFractionalBits>>
  explicit constexpr Fixed(const Fixed<OtherInteger, OtherFractionalBits>& other)
      : Fixed{Format::Convert(other.value())} {}

  // Assignment from an intermediate expression. The value is rounded and
  // saturated to fit within the precision and resolution of this type, if
  // necessary.
  template <Operation Op, typename... Args>
  constexpr Fixed& operator=(Expression<Op, Args...> expression) {
    return *this = Fixed{expression};
  }

  // Implicit conversion from an intermediate value of the same format.
  constexpr Fixed(Value<Format> value) : value_{Format::Saturate(value)} {}

  // Assignment from an intermediate value of the same format.
  constexpr Fixed& operator=(Value<Format> value) { return *this = Fixed{value}; }

  // Returns the raw fixed-point value as the underling integer type.
  constexpr Integer raw_value() const { return value_; }

  // Returns the fixed-point value as an intermediate value type.
  constexpr Value<Format> value() const { return Value<Format>{value_}; }

  // Returns the closest integer value greater-than or equal-to this fixed-
  // point value.
  constexpr Integer Ceiling() const {
    const Integer value = value_ / Format::AdjustmentFactor;
    const Integer power = Format::AdjustedPower;
    const auto saturated_value = SaturateAddAs<Integer>(value, Format::AdjustedFractionalMask);
    return static_cast<Integer>(saturated_value / power);
  }

  // Returns the closest integer value less-than or equal-to this fixed-point
  // value.
  constexpr Integer Floor() const {
    const Integer value = value_ / Format::AdjustmentFactor;
    const Integer power = Format::AdjustedPower;
    const Integer masked_value = value & Format::AdjustedIntegralMask;
    return static_cast<Integer>(masked_value / power);
  }

  // Returns the rounded value of this fixed-point value as an integer.
  constexpr Integer Round() const {
    const Integer value = value_ / Format::AdjustmentFactor;
    const Integer power = Format::AdjustedPower;
    const Integer rounded_value = Format::Round(value, ToPlace<Format::AdjustedFractionalBits>);
    return Format::Saturate(static_cast<Integer>(rounded_value / power));
  }

  // Returns the fractional component of this fixed-point value.
  constexpr Fixed Fraction() const { return *this - Fixed{Floor()}; }

  // Returns the absolute value of this fixed-point value.
  constexpr Fixed Absolute() {
    // Compute a mask and bit to conditionally convert |value_| to positive.
    // When |value_| is negative, then |mask| = -1 and |one| = 1, otherwise both
    // are zero.
    const Integer mask = static_cast<Integer>(-(value_ < 0));
    const Integer one = mask & 1;

    // Find the absolute value by computing the clamped two's complement. This
    // is a no-op when |value_| is positive because |mask| and |one| are zero.
    // Note that this will always return a positive value by clamping to Max.
    Integer absolute = 0;
    if (__builtin_add_overflow(value_ ^ mask, one, &absolute)) {
      absolute = Format::Max;
    }
    return FromRaw(absolute);
  }

  // Relational operators for same-typed values.
  constexpr bool operator<(Fixed other) const { return value_ < other.value_; }
  constexpr bool operator>(Fixed other) const { return value_ > other.value_; }
  constexpr bool operator<=(Fixed other) const { return value_ <= other.value_; }
  constexpr bool operator>=(Fixed other) const { return value_ >= other.value_; }
  constexpr bool operator==(Fixed other) const { return value_ == other.value_; }
  constexpr bool operator!=(Fixed other) const { return value_ != other.value_; }

  // Compound assignment operators.
  template <typename T, typename Enabled = EnableIfUnaryExpression<T>>
  constexpr Fixed& operator+=(T expression) {
    *this = *this + expression;
    return *this;
  }
  template <typename T, typename Enabled = EnableIfUnaryExpression<T>>
  constexpr Fixed& operator-=(T expression) {
    *this = *this - expression;
    return *this;
  }
  template <typename T, typename Enabled = EnableIfUnaryExpression<T>>
  constexpr Fixed& operator*=(T expression) {
    *this = *this * expression;
    return *this;
  }
  template <typename T, typename Enabled = EnableIfUnaryExpression<T>>
  constexpr Fixed& operator/=(T expression) {
    *this = *this / expression;
    return *this;
  }

 private:
  Integer value_;
};

// Utility to round an expression to the given Integer.
template <typename Integer, typename T, typename Enabled = EnableIfUnaryExpression<T>>
inline constexpr auto Round(T expression) {
  const Fixed<Integer, 0> value{ToExpression<T>{expression}};
  return value.Round();
}

// Utility to create an Expression node from an integer value.
template <typename Integer, typename Enabled = std::enable_if_t<std::is_integral_v<Integer>>>
inline constexpr auto FromInteger(Integer value) {
  return ToExpression<Integer>{value};
}

// Utility to create an Expression node from an integer ratio. May be used to
// initialize a Fixed variable from a ratio.
template <typename Integer, typename Enabled = std::enable_if_t<std::is_integral_v<Integer>>>
inline constexpr auto FromRatio(Integer numerator, Integer denominator) {
  return DivisionExpression<Integer, Integer>{numerator, denominator};
}

// Utility to coerce an expression to the given resolution.
template <size_t FractionalBits, typename T>
inline constexpr auto ToResolution(T expression) {
  return ResolutionExpression<FractionalBits, T>{Init{}, expression};
}

// Utility to create a value Expression from a raw integer value already in the
// fixed-point format with the given number of fractional bits.
template <size_t FractionalBits, typename Integer>
inline constexpr auto FromRaw(Integer value) {
  return ValueExpression<Integer, FractionalBits>{value};
}

// Relational operators.
//
// Fixed-to-fixed comparisons convert to an intermediate type with suitable
// precision and the least resolution of the two operands, using convergent
// rounding to reduce resolution and avoid bias.
//
// Fixed-to-integer comparisons convert to an intermediate type with suitable
// precision and the resolution of the fixed-point type. This is less
// less surprising when comparing a fixed-point type to zero and other integer
// constants.
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator<(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) < Traits::Right(right);
}
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator>(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) > Traits::Right(right);
}
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator<=(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) <= Traits::Right(right);
}
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator>=(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) >= Traits::Right(right);
}
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator==(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) == Traits::Right(right);
}
template <typename Left, typename Right,
          typename Enabled = EnableIfComparisonExpression<Left, Right>>
inline constexpr bool operator!=(Left left, Right right) {
  using Traits = ComparisonTraits<Left, Right>;
  return Traits::Left(left) != Traits::Right(right);
}

// Arithmetic operators. These operators accept any combination of Fixed,
// integer, and Expression (excluding integer/integer which is handled by the
// language). The return type and value captures the operation and operands as
// an Expression for later evaluation. Evaluation is performed when the
// Expression tree is assigned to a Fixed variable. This can be composed in
// multiple stages and assignments.
//
// Example:
//
//     const int32_t value = ...;
//     cosnt int32_t offset = ...;
//
//     const auto quotient = FromRatio(value, 3);
//     const Fixed<int32_t, 1> low_precision = quotient;
//     const Fixed<int64_t, 10> high_precision = quotient;
//
//     const auto with_offset = quotient + ToResolution<10>(offset);
//     const Fixed<int64_t, 10> high_precision_with_offset = with_offset;
//
template <typename Left, typename Right, typename Enabled = EnableIfBinaryExpression<Left, Right>>
inline constexpr auto operator+(Left left, Right right) {
  return AdditionExpression<Left, Right>{left, right};
}
template <typename T, typename Enabled = EnableIfUnaryExpression<T>>
inline constexpr auto operator-(T value) {
  return NegationExpression<T>{Init{}, value};
}
template <typename Left, typename Right, typename Enabled = EnableIfBinaryExpression<Left, Right>>
inline constexpr auto operator-(Left left, Right right) {
  return SubtractionExpression<Left, Right>{left, right};
}
template <typename Left, typename Right, typename Enabled = EnableIfBinaryExpression<Left, Right>>
inline constexpr auto operator*(Left left, Right right) {
  return MultiplicationExpression<Left, Right>{left, right};
}
template <typename Left, typename Right, typename Enabled = EnableIfBinaryExpression<Left, Right>>
inline constexpr auto operator/(Left left, Right right) {
  return DivisionExpression<Left, Right>{left, right};
}

}  // namespace ffl

#endif  // FFL_FIXED_H_
