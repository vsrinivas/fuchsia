// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <ffl/fixed_format.h>

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
    explicit constexpr Expression(Integer raw_value)
        : value{raw_value} {}

    // Constructs the expression node from a Fixed instance of the same format.
    explicit constexpr Expression(Fixed<Integer, FractionalBits> fixed)
        : value{fixed.raw_value()} {}

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
    constexpr Expression(Init, T&& value)
        : value{std::forward<T>(value)} {}

    const Expression<Op, Args...> value;

    template <typename TargetFormat>
    constexpr auto Evaluate(TargetFormat target_format) const {
        return Perform(value.Evaluate(target_format));
    }

private:
    template <typename Format>
    static constexpr auto Perform(Value<Format> value) {
        return Value<Format>{-value.value};
    }
};

// Specialization to coerce the precision of a subexpression. This expression
// node takes template arguments for the target precision and subexpression to
// coerce.
template <size_t FractionalBits, Operation Op, typename... Args>
struct Expression<Operation::Resolution, Resolution<FractionalBits>,
                  Expression<Op, Args...>> {
    template <typename T>
    constexpr Expression(Init, T&& value)
        : value{std::forward<T>(value)} {}

    const Expression<Op, Args...> value;

    template <typename TargetFormat>
    constexpr auto Evaluate(TargetFormat) const {
        using Intermediate = typename TargetFormat::Integer;
        using IntermediateFormat = FixedFormat<Intermediate, FractionalBits>;
        return IntermediateFormat::Convert(value.Evaluate(IntermediateFormat{}));
    }
};

// Specialization for addition of subexpressions. This expression node takes two
// template arguments for the left-hand and right-hand subexpressions to add.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Addition,
                  Expression<LeftOp, LeftArgs...>,
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
        // Select the intermediate precision based on the argument format with
        // the largest precision.
        using Integer = std::conditional_t<LeftFormat::Bits >= RightFormat::Bits,
                                           typename LeftFormat::Integer,
                                           typename RightFormat::Integer>;
        // Select the intermediate resolution based on the argument format with
        // the least resolution.
        const size_t FractionalBits = std::min(LeftFormat::FractionalBits,
                                               RightFormat::FractionalBits);

        using IntermediateFormat = FixedFormat<Integer, FractionalBits>;
        using Intermediate = typename IntermediateFormat::Intermediate;

        const auto left_value = IntermediateFormat::Convert(left);
        const auto right_value = IntermediateFormat::Convert(right);
        const auto value = static_cast<Intermediate>(left_value.value + right_value.value);

        return Value<IntermediateFormat>{value};
    }
};

// Specialization for subtraction of subexpressions. This expression node takes
// two template arguments for the left-hand and right-hand subexpressions to
// subtract.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Subtraction,
                  Expression<LeftOp, LeftArgs...>,
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
        // Select the intermediate precision based on the argument format with
        // the largest precision.
        using Integer = std::conditional_t<LeftFormat::Bits >= RightFormat::Bits,
                                           typename LeftFormat::Integer,
                                           typename RightFormat::Integer>;
        // Select the intermediate resolution based on the argument format with
        // the least resolution.
        const size_t FractionalBits = std::min(LeftFormat::FractionalBits,
                                               RightFormat::FractionalBits);

        using IntermediateFormat = FixedFormat<Integer, FractionalBits>;
        using Intermediate = typename IntermediateFormat::Intermediate;

        const auto left_value = IntermediateFormat::Convert(left);
        const auto right_value = IntermediateFormat::Convert(right);
        const auto value = static_cast<Intermediate>(left_value.value - right_value.value);

        return Value<IntermediateFormat>{value};
    }
};

// Specialization for multiplication of subexpressions. This expression node
// takes two template arguments for the left-hand and right-hand subexpressions
// to multiply.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Multiplication,
                  Expression<LeftOp, LeftArgs...>,
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
        // Select the intermediate precision based on the argument format with
        // the largest precision. The intermediate format precision is
        // guaranteed to be sufficient for multiplication with itself or
        // smaller, unless the type is already maximally wide. It is up to the
        // user to ensure that multiplication does not exceed the maximum
        // supported precision.
        using Integer = std::conditional_t<LeftFormat::Bits >= RightFormat::Bits,
                                                typename LeftFormat::Intermediate,
                                                typename RightFormat::Intermediate>;
        const size_t FractionalBits = LeftFormat::FractionalBits + RightFormat::FractionalBits;
        using IntermediateFormat = FixedFormat<Integer, FractionalBits>;
        using Intermediate = typename IntermediateFormat::Intermediate;

        const Integer left_value = left.value;
        const Integer right_value = right.value;
        const auto value = static_cast<Intermediate>(left_value * right_value);

        return Value<IntermediateFormat>{value};
    }
};

// Specialization for division of subexpressions. This expression node takes two
// template arguments for the left-hand and right-hand subexpressions to divide.
template <Operation LeftOp, Operation RightOp, typename... LeftArgs, typename... RightArgs>
struct Expression<Operation::Division,
                  Expression<LeftOp, LeftArgs...>,
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
        // Select the intermediate precision based on the format with the
        // largest precision. The intermediate format precision is guaranteed
        // to be sufficient for division with itself or smaller, unless the type
        // is already maximally wide. It is up to the user to ensure that
        // division does not exceed the maximum supported precision.
        using LeftOrRightFormat = std::conditional_t<LeftFormat::Bits >= RightFormat::Bits,
                                                     LeftFormat, RightFormat>;
        using Intermediate = std::conditional_t<LeftOrRightFormat::Bits >= TargetFormat::Bits,
                                                typename LeftOrRightFormat::Intermediate,
                                                typename TargetFormat::Intermediate>;
        const size_t FractionalBits = TargetFormat::FractionalBits + RightFormat::FractionalBits;
        using IntermediateFormat = FixedFormat<Intermediate, FractionalBits>;
        using QuotientFormat = FixedFormat<Intermediate, TargetFormat::FractionalBits>;

        const auto left_value = IntermediateFormat::Convert(left).value;
        return Value<QuotientFormat>{left_value / right.value};
    }
};

// Traits type to determine whether some type T may be converted to
// an Expression and the specific type of Expression it converts to.
template <typename T, typename Enabled = void>
struct ExpressionTraits : std::false_type {};

template <typename Integer, size_t FractionalBits>
struct ExpressionTraits<Fixed<Integer, FractionalBits>> : std::true_type {
    using ExpressionType = Expression<Operation::Value,
                                      typename Fixed<Integer, FractionalBits>::Format>;
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
// the format with the least resolution before comparison.
template <typename LeftInteger, size_t LeftFractionalBits,
          typename RightInteger, size_t RightFractionalBits>
struct ComparisonTraits<Fixed<LeftInteger, LeftFractionalBits>,
                        Fixed<RightInteger, RightFractionalBits>> : std::true_type {
    static constexpr auto Left(Fixed<LeftInteger, LeftFractionalBits> value) {
        if constexpr (LeftFractionalBits <= RightFractionalBits) {
            return value;
        } else {
            using Format = typename Fixed<RightInteger, RightFractionalBits>::Format;
            return Format::Convert(value.value());
        }
    }

    static constexpr auto Right(Fixed<RightInteger, RightFractionalBits> value) {
        if constexpr (LeftFractionalBits > RightFractionalBits) {
            return value;
        } else {
            using Format = typename Fixed<LeftInteger, LeftFractionalBits>::Format;
            return Format::Convert(value.value());
        }
    }
};

// Specialization for comparing Fixed with Expression. The expression is
// evaluated and converted to the same format as Fixed before comparison.
template <typename Integer, size_t FractionalBits, Operation Op,
          typename... Args>
struct ComparisonTraits<Fixed<Integer, FractionalBits>, Expression<Op, Args...>> : std::true_type {
    static constexpr auto Left(Fixed<Integer, FractionalBits> value) {
        return value;
    }
    static constexpr auto Right(Expression<Op, Args...> expression) {
        return Fixed<Integer, FractionalBits>{expression};
    }
};

// Specialization for comparing Expression with Fixed. The expression is
// evaluated and converted to the same format as Fixed before comparison.
template <typename Integer, size_t FractionalBits, Operation Op,
          typename... Args>
struct ComparisonTraits<Expression<Op, Args...>, Fixed<Integer, FractionalBits>> : std::true_type {
    static constexpr auto Right(Expression<Op, Args...> expression) {
        return Fixed<Integer, FractionalBits>{expression};
    }
    static constexpr auto Right(Fixed<Integer, FractionalBits> value) {
        return value;
    }
};

// Specialization for comparing Fixed with integer. The Fixed is converted to
// integer format before comparison.
template <typename Integer, size_t FractionalBits, typename T>
struct ComparisonTraits<Fixed<Integer, FractionalBits>, T, std::enable_if_t<std::is_integral_v<T>>>
    : std::true_type {
    using TargetType = Fixed<T, 0>;
    static constexpr auto Left(Fixed<Integer, FractionalBits> value) {
        return TargetType{TargetType::Format::Convert(value.value())};
    }
    static constexpr auto Right(T value) {
        return TargetType{ToExpression<T>(value)};
    }
};

// Specialization for comparing integer with Fixed. The Fixed is converted to
// integer format before comparison.
template <typename Integer, size_t FractionalBits, typename T>
struct ComparisonTraits<T, Fixed<Integer, FractionalBits>, std::enable_if_t<std::is_integral_v<T>>>
    : std::true_type {
    using TargetType = Fixed<T, 0>;
    static constexpr auto Left(T value) {
        return TargetType{ToExpression<T>(value)};
    }
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
using ResolutionExpression = Expression<Operation::Resolution,
                                        Resolution<FractionalBits>,
                                        ToExpression<T>>;

// Alias for an addition expression node type.
template <typename Left, typename Right>
using AdditionExpression = Expression<Operation::Addition,
                                      ToExpression<Left>,
                                      ToExpression<Right>>;

// Alias for an subtraction expression node type.
template <typename Left, typename Right>
using SubtractionExpression = Expression<Operation::Subtraction,
                                         ToExpression<Left>,
                                         ToExpression<Right>>;

// Alias for an multiplication expression node type.
template <typename Left, typename Right>
using MultiplicationExpression = Expression<Operation::Multiplication,
                                            ToExpression<Left>,
                                            ToExpression<Right>>;

// Alias for an multiplication expression node type.
template <typename Left, typename Right>
using DivisionExpression = Expression<Operation::Division,
                                      ToExpression<Left>,
                                      ToExpression<Right>>;

// Enable if T can be converted into a unary expression node.
template <typename T, typename Return = void>
using EnableIfUnaryExpression = std::enable_if_t<ExpressionTraits<T>::value, Return>;

// Enable if T and U can be converted into a binary expression node.
template <typename T, typename U, typename Return = void>
using EnableIfBinaryExpression =
    std::enable_if_t<ExpressionTraits<T>::value && ExpressionTraits<U>::value &&
                         !(std::is_integral_v<T> && std::is_integral_v<U>),
                     Return>;

} // namespace ffl
