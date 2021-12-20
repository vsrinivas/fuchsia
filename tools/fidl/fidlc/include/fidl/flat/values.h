// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VALUES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VALUES_H_

#include <iostream>
#include <limits>
#include <type_traits>

#include "fidl/flat/name.h"
#include "fidl/raw_ast.h"
#include "fidl/source_span.h"
#include "fidl/types.h"

namespace fidl::flat {

struct Type;

// ConstantValue represents the concrete _value_ of a constant. (For the
// _declaration_, see Const. For the _use_, see Constant.) ConstantValue has
// derived classes for all the different kinds of constants.
struct ConstantValue {
  virtual ~ConstantValue() {}

  enum struct Kind {
    kInt8,
    kInt16,
    kInt32,
    kInt64,
    kUint8,
    kUint16,
    kUint32,
    kUint64,
    kFloat32,
    kFloat64,
    kBool,
    kString,
    kDocComment,
  };

  virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const = 0;

  const Kind kind;

 protected:
  explicit ConstantValue(Kind kind) : kind(kind) {}
};

template <typename ValueType>
struct NumericConstantValue final : ConstantValue {
  static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, bool>::value,
                "NumericConstantValue can only be used with a numeric ValueType!");

  NumericConstantValue(ValueType value) : ConstantValue(GetKind()), value(value) {}

  operator ValueType() const { return value; }

  friend bool operator==(const NumericConstantValue<ValueType>& l,
                         const NumericConstantValue<ValueType>& r) {
    return l.value == r.value;
  }

  friend bool operator<(const NumericConstantValue<ValueType>& l,
                        const NumericConstantValue<ValueType>& r) {
    return l.value < r.value;
  }

  friend bool operator>(const NumericConstantValue<ValueType>& l,
                        const NumericConstantValue<ValueType>& r) {
    return l.value > r.value;
  }

  friend bool operator!=(const NumericConstantValue<ValueType>& l,
                         const NumericConstantValue<ValueType>& r) {
    return l.value != r.value;
  }

  friend bool operator<=(const NumericConstantValue<ValueType>& l,
                         const NumericConstantValue<ValueType>& r) {
    return l.value <= r.value;
  }

  friend bool operator>=(const NumericConstantValue<ValueType>& l,
                         const NumericConstantValue<ValueType>& r) {
    return l.value >= r.value;
  }

  friend NumericConstantValue<ValueType> operator|(const NumericConstantValue<ValueType>& l,
                                                   const NumericConstantValue<ValueType>& r) {
    static_assert(!std::is_same_v<ValueType, float> && !std::is_same_v<ValueType, double>);
    return NumericConstantValue<ValueType>(l.value | r.value);
  }

  friend std::ostream& operator<<(std::ostream& os, const NumericConstantValue<ValueType>& v) {
    if constexpr (GetKind() == Kind::kInt8)
      return os << static_cast<int>(v.value);
    if constexpr (GetKind() == Kind::kUint8)
      return os << static_cast<unsigned>(v.value);
    return os << v.value;
  }

  bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override;

  static NumericConstantValue<ValueType> Min() {
    return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::lowest());
  }

  static NumericConstantValue<ValueType> Max() {
    return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::max());
  }

  ValueType value;

 private:
  constexpr static Kind GetKind() {
    if constexpr (std::is_same_v<ValueType, uint64_t>)
      return Kind::kUint64;
    if constexpr (std::is_same_v<ValueType, int64_t>)
      return Kind::kInt64;
    if constexpr (std::is_same_v<ValueType, uint32_t>)
      return Kind::kUint32;
    if constexpr (std::is_same_v<ValueType, int32_t>)
      return Kind::kInt32;
    if constexpr (std::is_same_v<ValueType, uint16_t>)
      return Kind::kUint16;
    if constexpr (std::is_same_v<ValueType, int16_t>)
      return Kind::kInt16;
    if constexpr (std::is_same_v<ValueType, uint8_t>)
      return Kind::kUint8;
    if constexpr (std::is_same_v<ValueType, int8_t>)
      return Kind::kInt8;
    if constexpr (std::is_same_v<ValueType, double>)
      return Kind::kFloat64;
    if constexpr (std::is_same_v<ValueType, float>)
      return Kind::kFloat32;
  }
};

using Size = NumericConstantValue<uint32_t>;
using HandleRights = NumericConstantValue<types::RightsWrappedType>;

struct BoolConstantValue final : ConstantValue {
  BoolConstantValue(bool value) : ConstantValue(ConstantValue::Kind::kBool), value(value) {}

  operator bool() const { return value; }

  friend bool operator==(const BoolConstantValue& l, const BoolConstantValue& r) {
    return l.value == r.value;
  }

  friend bool operator!=(const BoolConstantValue& l, const BoolConstantValue& r) {
    return l.value != r.value;
  }

  friend std::ostream& operator<<(std::ostream& os, const BoolConstantValue& v) {
    return os << v.value;
  }

  bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override;

  bool value;
};

struct DocCommentConstantValue final : ConstantValue {
  explicit DocCommentConstantValue(std::string_view value)
      : ConstantValue(ConstantValue::Kind::kDocComment), value(value) {}

  friend std::ostream& operator<<(std::ostream& os, const DocCommentConstantValue& v) {
    return os << v.value.data();
  }

  bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const;
  std::string MakeContents() const;

  std::string_view value;
};

struct StringConstantValue final : ConstantValue {
  explicit StringConstantValue(std::string_view value)
      : ConstantValue(ConstantValue::Kind::kString), value(value) {}

  friend std::ostream& operator<<(std::ostream& os, const StringConstantValue& v) {
    os << v.value.data();
    return os;
  }

  bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const;
  std::string MakeContents() const;

  std::string_view value;
};

// Constant represents the _use_ of a constant. (For the _declaration_, see
// Const. For the _value_, see ConstantValue.) A Constant can either be a
// reference to another constant (IdentifierConstant), a literal value
// (LiteralConstant). Every Constant resolves to a concrete ConstantValue.
struct Constant {
  virtual ~Constant() {}

  enum struct Kind { kIdentifier, kLiteral, kBinaryOperator };

  explicit Constant(Kind kind, SourceSpan span) : kind(kind), span(span), value_(nullptr) {}

  bool IsResolved() const { return value_ != nullptr; }
  void ResolveTo(std::unique_ptr<ConstantValue> value, const Type* type);
  const ConstantValue& Value() const;

  const Kind kind;
  const SourceSpan span;
  // compiled tracks whether we attempted to resolve this constant, to avoid
  // resolving twice a constant which cannot be resolved.
  bool compiled = false;

  // set when resolved to
  const Type* type = nullptr;

 protected:
  std::unique_ptr<ConstantValue> value_;
};

struct IdentifierConstant final : Constant {
  explicit IdentifierConstant(Name name, SourceSpan span)
      : Constant(Kind::kIdentifier, span), name(std::move(name)) {}

  const Name name;
};

struct LiteralConstant final : Constant {
  explicit LiteralConstant(std::unique_ptr<raw::Literal> literal);

  std::unique_ptr<raw::Literal> literal;
};

struct BinaryOperatorConstant final : Constant {
  enum struct Operator { kOr };

  explicit BinaryOperatorConstant(std::unique_ptr<Constant> left_operand,
                                  std::unique_ptr<Constant> right_operand, Operator op,
                                  SourceSpan span)
      : Constant(Kind::kBinaryOperator, span),
        left_operand(std::move(left_operand)),
        right_operand(std::move(right_operand)),
        op(op) {}

  const std::unique_ptr<Constant> left_operand;
  const std::unique_ptr<Constant> right_operand;
  const Operator op;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_VALUES_H_
