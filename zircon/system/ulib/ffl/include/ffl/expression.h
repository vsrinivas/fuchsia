// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef FFL_EXPRESSION_H_
#define FFL_EXPRESSION_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ffl/fixed_format.h>
#include <ffl/saturating_arithmetic.h>
#include <ffl/utility.h>

namespace ffl {

// Forward declaration.
template <typename Integer, size_t FractionalBits>
class Fixed;

// Enumeration representing the type or function of an Expression.
enum class Operation {
  Value,
  Addition,
  Subtraction,
  Multiplication,
  Division,
  Negation,
  Resolution,
};

// Traits type that determines the promoted result format, given an operation
// and input formats.
template <Operation, typename, typename, typename = void>
struct PromoteFormat;

template <typename SourceFormat, typename TargetFormat>
struct PromoteFormat<Operation::Value, SourceFormat, TargetFormat> {
  static constexpr bool IsSigned = std::is_signed_v<typename TargetFormat::Integer>;

  static constexpr size_t FractionalBits = TargetFormat::FractionalBits;

  using Integer =
      BestFitting<IsSigned, std::max(SourceFormat::IntegralBits, TargetFormat::IntegralBits) +
                                FractionalBits>;
  using Format = FixedFormat<Integer, FractionalBits>;
};

template <typename LeftFormat, typename RightFormat>
struct PromoteFormat<Operation::Addition, LeftFormat, RightFormat> {
  static constexpr bool IsSigned =
      std::is_signed_v<decltype(std::declval<typename LeftFormat::Integer>() +
                                std::declval<typename RightFormat::Integer>())>;

  static constexpr size_t FractionalBits =
      std::min(LeftFormat::FractionalBits, RightFormat::FractionalBits);

  using Integer =
      BestFitting<IsSigned, std::max(LeftFormat::PositiveBits, RightFormat::PositiveBits) + 1>;
  using Format = FixedFormat<Integer, FractionalBits>;
};

template <typename LeftFormat, typename RightFormat>
struct PromoteFormat<Operation::Subtraction, LeftFormat, RightFormat> {
  static constexpr bool IsSigned =
      std::is_signed_v<decltype(std::declval<typename LeftFormat::Integer>() -
                                std::declval<typename RightFormat::Integer>())>;

  static constexpr size_t FractionalBits =
      std::min(LeftFormat::FractionalBits, RightFormat::FractionalBits);

  using Integer =
      BestFitting<IsSigned, std::max(LeftFormat::PositiveBits, RightFormat::PositiveBits) + 1>;
  using Format = FixedFormat<Integer, FractionalBits>;
};

template <typename LeftFormat, typename RightFormat>
struct PromoteFormat<Operation::Multiplication, LeftFormat, RightFormat> {
  static constexpr bool IsSigned =
      std::is_signed_v<decltype(std::declval<typename LeftFormat::Integer>() *
                                std::declval<typename RightFormat::Integer>())>;

  static constexpr size_t FractionalBits = LeftFormat::FractionalBits + RightFormat::FractionalBits;
  static constexpr size_t IntegralBits =
      LeftFormat::IntegralBits + RightFormat::IntegralBits + (IsSigned ? 1 : 0);

  using Integer = BestFitting<IsSigned, IntegralBits + FractionalBits>;
  using Format = FixedFormat<Integer, FractionalBits>;
};

template <typename LeftFormat, typename RightFormat, typename TargetFormat>
struct PromoteFormat<Operation::Division, LeftFormat, RightFormat, TargetFormat> {
  static constexpr bool IsSigned =
      std::is_signed_v<decltype(std::declval<typename LeftFormat::Integer>() /
                                std::declval<typename RightFormat::Integer>())>;

  static constexpr size_t FractionalBits =
      TargetFormat::FractionalBits + RightFormat::FractionalBits;
  static constexpr size_t IntegralBits =
      LeftFormat::IntegralBits + RightFormat::FractionalBits + (IsSigned ? 1 : 0);

  using Integer = BestFitting<IsSigned, IntegralBits + FractionalBits>;
  using NumeratorFormat = FixedFormat<Integer, FractionalBits>;
  using QuotientFormat = FixedFormat<Integer, TargetFormat::FractionalBits>;
};

// Type representing a node in an expression tree. Specializations implement the
// various types of expression nodes and their behavior. A specialization must
// have a template method to perform evaluation compatible with the following
// signature:
//
// template <typename TargetFormat>
// constexpr auto Evaluate(TargetFormat) const { ... }
//
// The |TargetFormat| template parameter is an instantiation of FixedFormat to
// provide a hint about the final format of the evaluated expression. This may
// be used to make resolution optimization decisions however, the result of the
// Evaluate method is not required to be in TargetFormat.
//
// The return value of Evaluate must be an instance of Value<> and may be in any
// format suitable to the result of the expression node evaluation.
//
template <Operation, typename... Args>
struct Expression;

// Specialization for immediate values in a particular format. This expression
// node takes a single template argument for the format of the value to store.
template <typename Integer, size_t FractionalBits>
struct Expression<Operation::Value, FixedFormat<Integer, FractionalBits>> {
  using Format = FixedFormat<Integer, FractionalBits>;

  // Constructs the expression node from a raw integer value already in the
  // fixed-point format specified by Format.
  explicit constexpr Expression(Integer raw_value) : value{raw_value} {}

  // Constructs the expression node from a Fixed instance of the same format.
  explicit constexpr Expression(Fixed<Integer, FractionalBits> fixed) : value{fixed.raw_value()} {}

  const Value<Format> value;

  // Returns the underlying value. TargetFormat is ignored, conversion to the
  // final format is handled by the Fixed constructor or assignment operator.
  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat) const {
    return value;
  }
};

// Specialization for negation of a subexpression. This expression node takes a
// single template argument for the subexpression to negate.
template <Operation Op, typename... Args>
struct Expression<Operation::Negation, Expression<Op, Args...>> {
  template <typename T>
  constexpr Expression(Init, T&& value) : value{std::forward<T>(value)} {}

  const Expression<Op, Args...> value;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat target_format) const {
    return Perform(value.Evaluate(target_format));
  }

 private:
  template <typename TargetFormat>
  static constexpr auto Perform(Value<TargetFormat> value) {
    using Integer = typename TargetFormat::Integer;
    const Integer result = -value.value;
    return Value<TargetFormat>{result};
  }
};

// Specialization to coerce the precision of a subexpression. This expression
// node takes template arguments for the target precision and subexpression to
// coerce.
template <size_t FractionalBits, Operation Op, typename... Args>
struct Expression<Operation::Resolution, Resolution<FractionalBits>, Expression<Op, Args...>> {
  template <typename T>
  constexpr Expression(Init, T&& value) : value{std::forward<T>(value)} {}

  const Expression<Op, Args...> value;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat) const {
    using Integer =
        BestFitting<TargetFormat::IsSigned, TargetFormat::IntegralBits + FractionalBits>;
    using IntermediateFormat = FixedFormat<Integer, FractionalBits>;
    return IntermediateFormat::Convert(value.Evaluate(IntermediateFormat{}));
  }
};

// Specialization for addition of subexpressions. This expression node takes two
// template arguments for the left-hand and right-hand subexpressions to add.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Addition, Expression<LeftOp, LeftArgs...>,
                  Expression<RightOp, RightArgs...>> {
  template <typename L, typename R>
  constexpr Expression(L&& left, R&& right)
      : left{std::forward<L>(left)}, right{std::forward<R>(right)} {}

  const Expression<LeftOp, LeftArgs...> left;
  const Expression<RightOp, RightArgs...> right;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat target_format) const {
    return Perform(left.Evaluate(target_format), right.Evaluate(target_format));
  }

 private:
  template <typename LeftFormat, typename RightFormat>
  static constexpr auto Perform(Value<LeftFormat> left, Value<RightFormat> right) {
    using Promote = PromoteFormat<Operation::Addition, LeftFormat, RightFormat>;
    using IntermediateFormat = typename Promote::Format;
    using Integer = typename IntermediateFormat::Integer;

    const auto left_value = IntermediateFormat::Convert(left);
    const auto right_value = IntermediateFormat::Convert(right);

    return Value<IntermediateFormat>{SaturateAddAs<Integer>(left_value.value, right_value.value)};
  }
};

// Specialization for subtraction of subexpressions. This expression node takes
// two template arguments for the left-hand and right-hand subexpressions to
// subtract.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Subtraction, Expression<LeftOp, LeftArgs...>,
                  Expression<RightOp, RightArgs...>> {
  template <typename L, typename R>
  constexpr Expression(L&& left, R&& right)
      : left{std::forward<L>(left)}, right{std::forward<R>(right)} {}

  const Expression<LeftOp, LeftArgs...> left;
  const Expression<RightOp, RightArgs...> right;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat target_format) const {
    return Perform(left.Evaluate(target_format), right.Evaluate(target_format));
  }

 private:
  template <typename LeftFormat, typename RightFormat>
  static constexpr auto Perform(Value<LeftFormat> left, Value<RightFormat> right) {
    using Promote = PromoteFormat<Operation::Subtraction, LeftFormat, RightFormat>;
    using IntermediateFormat = typename Promote::Format;
    using Integer = typename IntermediateFormat::Integer;

    const auto left_value = IntermediateFormat::Convert(left);
    const auto right_value = IntermediateFormat::Convert(right);

    return Value<IntermediateFormat>{
        SaturateSubtractAs<Integer>(left_value.value, right_value.value)};
  }
};

// Specialization for multiplication of subexpressions. This expression node
// takes two template arguments for the left-hand and right-hand subexpressions
// to multiply.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Multiplication, Expression<LeftOp, LeftArgs...>,
                  Expression<RightOp, RightArgs...>> {
  template <typename L, typename R>
  constexpr Expression(L&& left, R&& right)
      : left{std::forward<L>(left)}, right{std::forward<R>(right)} {}

  const Expression<LeftOp, LeftArgs...> left;
  const Expression<RightOp, RightArgs...> right;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat target_format) const {
    return Perform(left.Evaluate(target_format), right.Evaluate(target_format));
  }

 private:
  template <typename LeftFormat, typename RightFormat>
  static constexpr auto Perform(Value<LeftFormat> left, Value<RightFormat> right) {
    using Promote = PromoteFormat<Operation::Multiplication, LeftFormat, RightFormat>;
    using IntermediateFormat = typename Promote::Format;
    using Integer = typename IntermediateFormat::Integer;

    return Value<IntermediateFormat>{SaturateMultiplyAs<Integer>(left.value, right.value)};
  }
};

// Specialization for division of subexpressions. This expression node takes two
// template arguments for the left-hand and right-hand subexpressions to divide.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Division, Expression<LeftOp, LeftArgs...>,
                  Expression<RightOp, RightArgs...>> {
  template <typename L, typename R>
  constexpr Expression(L&& left, R&& right)
      : left{std::forward<L>(left)}, right{std::forward<R>(right)} {}

  const Expression<LeftOp, LeftArgs...> left;
  const Expression<RightOp, RightArgs...> right;

  template <typename TargetFormat>
  constexpr auto Evaluate(TargetFormat target_format) const {
    return Perform(target_format, left.Evaluate(target_format), right.Evaluate(target_format));
  }

 private:
  template <typename TargetFormat, typename LeftFormat, typename RightFormat>
  static constexpr auto Perform(TargetFormat, Value<LeftFormat> left, Value<RightFormat> right) {
    using Promote = PromoteFormat<Operation::Division, LeftFormat, RightFormat, TargetFormat>;
    using NumeratorFormat = typename Promote::NumeratorFormat;
    using QuotientFormat = typename Promote::QuotientFormat;
    using Integer = typename QuotientFormat::Integer;

    const auto quotient = NumeratorFormat::Convert(left).value / right.value;
    return Value<QuotientFormat>{static_cast<Integer>(quotient)};
  }
};

// Traits type to determine whether some type T may be converted to
// an Expression and the specific type of Expression it converts to.
template <typename T, typename Enabled = void>
struct ExpressionTraits : std::false_type {};

template <typename Integer, size_t FractionalBits>
struct ExpressionTraits<Fixed<Integer, FractionalBits>> : std::true_type {
  using ExpressionType =
      Expression<Operation::Value, typename Fixed<Integer, FractionalBits>::Format>;
};

template <Operation Op, typename... Args>
struct ExpressionTraits<Expression<Op, Args...>> : std::true_type {
  using ExpressionType = Expression<Op, Args...>;
};

template <typename T>
struct ExpressionTraits<T, std::enable_if_t<std::is_integral_v<T>>> : std::true_type {
  using ExpressionType = Expression<Operation::Value, FixedFormat<T, 0>>;
};

// Utility type to convert from T to its associated Expression.
template <typename T>
using ToExpression = typename ExpressionTraits<T>::ExpressionType;

// Traits type to determine whether two types may be compared. Provides Left and
// Right conversion operations to convert to a common format for comparison.
//
// Any combination of integer, Fixed<>, and Expression<> are supported,
// excluding integer-integer and Expression-Expression comparisons; integer-
// integer comparisons are already handled by the language, whereas Expression-
// Expression comparisons are excluded because expressions do not have a
// definite resolution until assigned.
//
// To compare two expressions explicitly convert at least one side to Fixed<>.
template <typename Right, typename Left, typename Enabled = void>
struct ComparisonTraits : std::false_type {};

// Specialization for comparison of two Fixed values. Values are converted to
// an common format with suitable precision and the least resolution.
template <typename LeftInteger, size_t LeftFractionalBits, typename RightInteger,
          size_t RightFractionalBits>
struct ComparisonTraits<
    Fixed<LeftInteger, LeftFractionalBits>, Fixed<RightInteger, RightFractionalBits>,
    std::enable_if_t<std::is_signed_v<LeftInteger> == std::is_signed_v<RightInteger>>>
    : std::true_type {
  // Extract the integral bits of each format.
  static constexpr size_t LeftIntegralBits =
      Fixed<LeftInteger, LeftFractionalBits>::Format::IntegralBits;
  static constexpr size_t RightIntegralBits =
      Fixed<RightInteger, RightFractionalBits>::Format::IntegralBits;

  // Use the least of the fractional bits of each format.
  static constexpr size_t TargetFractionalBits = std::min(LeftFractionalBits, RightFractionalBits);
  static constexpr size_t TargetIntegralBits = std::max(LeftIntegralBits, RightIntegralBits);

  static constexpr bool IsSigned = std::is_signed_v<LeftInteger> && std::is_signed_v<RightInteger>;

  // Use the best fitting integer that can accommodate the max range and min resolution.
  using TargetInteger = BestFitting<IsSigned, TargetIntegralBits + TargetFractionalBits>;
  using TargetType = Fixed<TargetInteger, TargetFractionalBits>;

  static constexpr auto Left(Fixed<LeftInteger, LeftFractionalBits> value) {
    return TargetType{TargetType::Format::Convert(value.value())};
  }

  static constexpr auto Right(Fixed<RightInteger, RightFractionalBits> value) {
    return TargetType{TargetType::Format::Convert(value.value())};
  }
};

// Specialization for comparing Fixed with Expression. The expression is
// evaluated and converted to the same format as Fixed before comparison.
template <typename Integer, size_t FractionalBits, Operation Op, typename... Args>
struct ComparisonTraits<Fixed<Integer, FractionalBits>, Expression<Op, Args...>> : std::true_type {
  static constexpr auto Left(Fixed<Integer, FractionalBits> value) { return value; }
  static constexpr auto Right(Expression<Op, Args...> expression) {
    return Fixed<Integer, FractionalBits>{expression};
  }
};

// Specialization for comparing Expression with Fixed. The expression is
// evaluated and converted to the same format as Fixed before comparison.
template <typename Integer, size_t FractionalBits, Operation Op, typename... Args>
struct ComparisonTraits<Expression<Op, Args...>, Fixed<Integer, FractionalBits>> : std::true_type {
  static constexpr auto Left(Expression<Op, Args...> expression) {
    return Fixed<Integer, FractionalBits>{expression};
  }
  static constexpr auto Right(Fixed<Integer, FractionalBits> value) { return value; }
};

// Specialization for comparing Fixed with integer. Both values are converted to
// a common format with suitable precision and the same resolution as the fixed
// argument.
template <typename Integer, size_t FractionalBits, typename T>
struct ComparisonTraits<
    Fixed<Integer, FractionalBits>, T,
    std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<Integer> == std::is_signed_v<T>>>
    : std::true_type {
  using TargetInteger =
      BestFitting<std::is_signed_v<Integer>,
                  std::max(IntegerPrecision<Integer>, IntegerPrecision<T> + FractionalBits)>;
  using TargetType = Fixed<TargetInteger, FractionalBits>;
  static constexpr auto Left(Fixed<Integer, FractionalBits> value) {
    return TargetType{TargetType::Format::Convert(value.value())};
  }
  static constexpr auto Right(T value) { return TargetType{ToExpression<T>(value)}; }
};

// Specialization for comparing integer with Fixed. Both values are converted to
// a common format with suitable precision and the same resolution as the fixed
// argument.
template <typename Integer, size_t FractionalBits, typename T>
struct ComparisonTraits<
    T, Fixed<Integer, FractionalBits>,
    std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<Integer> == std::is_signed_v<T>>>
    : std::true_type {
  using TargetInteger =
      BestFitting<std::is_signed_v<Integer>,
                  std::max(IntegerPrecision<Integer>, IntegerPrecision<T> + FractionalBits)>;
  using TargetType = Fixed<TargetInteger, FractionalBits>;
  static constexpr auto Left(T value) { return TargetType{ToExpression<T>(value)}; }
  static constexpr auto Right(Fixed<Integer, FractionalBits> value) {
    return TargetType{TargetType::Format::Convert(value.value())};
  }
};

// TODO(eieio): Integer-Expression comparisons.

// Enable if Left and Right are comparable.
template <typename Right, typename Left, typename Return = void>
using EnableIfComparisonExpression = std::enable_if_t<ComparisonTraits<Left, Right>::value, Return>;

// Alias for a value expression node type.
template <typename Integer, size_t FractionalBits>
using ValueExpression = Expression<Operation::Value, FixedFormat<Integer, FractionalBits>>;

// Alias for a negation expression node type.
template <typename T>
using NegationExpression = Expression<Operation::Negation, ToExpression<T>>;

// Alias for a precision expression node type.
template <size_t FractionalBits, typename T>
using ResolutionExpression =
    Expression<Operation::Resolution, Resolution<FractionalBits>, ToExpression<T>>;

// Alias for an addition expression node type.
template <typename Left, typename Right>
using AdditionExpression = Expression<Operation::Addition, ToExpression<Left>, ToExpression<Right>>;

// Alias for an subtraction expression node type.
template <typename Left, typename Right>
using SubtractionExpression =
    Expression<Operation::Subtraction, ToExpression<Left>, ToExpression<Right>>;

// Alias for an multiplication expression node type.
template <typename Left, typename Right>
using MultiplicationExpression =
    Expression<Operation::Multiplication, ToExpression<Left>, ToExpression<Right>>;

// Alias for an multiplication expression node type.
template <typename Left, typename Right>
using DivisionExpression = Expression<Operation::Division, ToExpression<Left>, ToExpression<Right>>;

// Enable if T can be converted into a unary expression node.
template <typename T, typename Return = void>
using EnableIfUnaryExpression = std::enable_if_t<ExpressionTraits<T>::value, Return>;

// Enable if T and U can be converted into a binary expression node.
template <typename T, typename U, typename Return = void>
using EnableIfBinaryExpression =
    std::enable_if_t<ExpressionTraits<T>::value && ExpressionTraits<U>::value &&
                         !(std::is_integral_v<T> && std::is_integral_v<U>),
                     Return>;

}  // namespace ffl

#endif  // FFL_EXPRESSION_H_
