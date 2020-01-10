// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#compilation
// for documentation

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_AST_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_AST_H_

#include <lib/fit/function.h>

#include <any>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <safemath/checked_math.h>

#include "attributes.h"
#include "error_reporter.h"
#include "raw_ast.h"
#include "type_shape.h"
#include "types.h"
#include "virtual_source_file.h"

namespace fidl {
namespace flat {

template <typename T>
struct PtrCompare {
  bool operator()(const T* left, const T* right) const { return *left < *right; }
};

class Typespace;
struct Decl;
class Library;

bool HasSimpleLayout(const Decl* decl);

// This is needed (for now) to work around declaration order issues.
std::string LibraryName(const Library* library, std::string_view separator);

// Name represents a scope name, i.e. a name within the context of a library
// or in the 'global' context. Names either reference (or name) things which
// appear in source, or are synthesized by the compiler (e.g. an anonymous
// struct name).
struct Name final {
  Name(const Library* library, const SourceSpan name)
      : library_(library), name_(name), member_name_(std::nullopt) {}

  Name(const Library* library, const SourceSpan name, const std::string member)
      : library_(library), name_(name), member_name_(member) {}

  Name(const Library* library, const std::string& name)
      : library_(library), name_(name), member_name_(std::nullopt) {}

  Name(Name&&) = default;
  Name(const Name&) = default;
  Name& operator=(Name&&) = default;

  const Library* library() const { return library_; }

  std::optional<SourceSpan> span() const {
    return std::holds_alternative<SourceSpan>(name_)
               ? std::make_optional(std::get<SourceSpan>(name_))
               : std::nullopt;
  }
  const std::string_view name_part() const {
    if (std::holds_alternative<AnonymousName>(name_)) {
      return std::get<AnonymousName>(name_);
    } else {
      return std::get<SourceSpan>(name_).data();
    }
  }
  const std::string name_full() const {
    auto name = std::string(name_part());
    if (member_name_.has_value()) {
      name.append(".");
      name.append(member_name_.value());
    }
    return name;
  }
  const std::optional<std::string> member_name() const { return member_name_; }
  const Name memberless_name() const {
    if (!member_name_) {
      return *this;
    }
    return Name(library_, name_, std::nullopt);
  }

  bool operator==(const Name& other) const {
    // can't use the library name yet, not necessarily compiled!
    auto library_ptr = reinterpret_cast<uintptr_t>(library_);
    auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
    return (library_ptr == other_library_ptr) && name_part() == other.name_part() &&
           member_name_ == other.member_name_;
  }
  bool operator!=(const Name& other) const { return !operator==(other); }

  bool operator<(const Name& other) const {
    // can't use the library name yet, not necessarily compiled!
    auto library_ptr = reinterpret_cast<uintptr_t>(library_);
    auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
    if (library_ptr != other_library_ptr)
      return library_ptr < other_library_ptr;
    if (name_part() != other.name_part())
      return name_part() < other.name_part();
    return member_name_ < other.member_name_;
  }

 private:
  using AnonymousName = std::string;

  Name(const Library* library, const std::variant<SourceSpan, AnonymousName>& name,
       std::optional<std::string> member_name)
      : library_(library), name_(name), member_name_(member_name) {}

  const Library* library_ = nullptr;
  std::variant<SourceSpan, AnonymousName> name_;
  // TODO(FIDL-705): Either a source span, or an anonymous member should be allowed.
  std::optional<std::string> member_name_;
};

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
      os << static_cast<int>(v.value);
    else if constexpr (GetKind() == Kind::kUint8)
      os << static_cast<unsigned>(v.value);
    else
      os << v.value;
    return os;
  }

  virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
    assert(out_value != nullptr);

    auto checked_value = safemath::CheckedNumeric<ValueType>(value);

    switch (kind) {
      case Kind::kInt8: {
        int8_t casted_value;
        if (!checked_value.template Cast<int8_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<int8_t>>(casted_value);
        return true;
      }
      case Kind::kInt16: {
        int16_t casted_value;
        if (!checked_value.template Cast<int16_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<int16_t>>(casted_value);
        return true;
      }
      case Kind::kInt32: {
        int32_t casted_value;
        if (!checked_value.template Cast<int32_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<int32_t>>(casted_value);
        return true;
      }
      case Kind::kInt64: {
        int64_t casted_value;
        if (!checked_value.template Cast<int64_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<int64_t>>(casted_value);
        return true;
      }
      case Kind::kUint8: {
        uint8_t casted_value;
        if (!checked_value.template Cast<uint8_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<uint8_t>>(casted_value);
        return true;
      }
      case Kind::kUint16: {
        uint16_t casted_value;
        if (!checked_value.template Cast<uint16_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<uint16_t>>(casted_value);
        return true;
      }
      case Kind::kUint32: {
        uint32_t casted_value;
        if (!checked_value.template Cast<uint32_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<uint32_t>>(casted_value);
        return true;
      }
      case Kind::kUint64: {
        uint64_t casted_value;
        if (!checked_value.template Cast<uint64_t>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<uint64_t>>(casted_value);
        return true;
      }
      case Kind::kFloat32: {
        float casted_value;
        if (!checked_value.template Cast<float>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<float>>(casted_value);
        return true;
      }
      case Kind::kFloat64: {
        double casted_value;
        if (!checked_value.template Cast<double>().AssignIfValid(&casted_value)) {
          return false;
        }
        *out_value = std::make_unique<NumericConstantValue<double>>(casted_value);
        return true;
      }
      case Kind::kString:
      case Kind::kBool:
        return false;
    }
  }

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
    os << v.value;
    return os;
  }

  virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
    assert(out_value != nullptr);
    switch (kind) {
      case Kind::kBool:
        *out_value = std::make_unique<BoolConstantValue>(value);
        return true;
      default:
        return false;
    }
  }

  bool value;
};

struct StringConstantValue final : ConstantValue {
  explicit StringConstantValue(std::string_view value)
      : ConstantValue(ConstantValue::Kind::kString), value(value) {}

  friend std::ostream& operator<<(std::ostream& os, const StringConstantValue& v) {
    os << v.value.data();
    return os;
  }

  virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
    assert(out_value != nullptr);
    switch (kind) {
      case Kind::kString:
        *out_value = std::make_unique<StringConstantValue>(std::string_view(value));
        return true;
      default:
        return false;
    }
  }

  std::string_view value;
};

// Constant represents the _use_ of a constant. (For the _declaration_, see
// Const. For the _value_, see ConstantValue.) A Constant can either be a
// reference to another constant (IdentifierConstant), a literal value
// (LiteralConstant), or synthesized by the compiler (SynthesizedConstant).
// Every Constant resolves to a concrete ConstantValue.
struct Constant {
  virtual ~Constant() {}

  enum struct Kind { kIdentifier, kLiteral, kSynthesized, kBinaryOperator };

  explicit Constant(Kind kind) : kind(kind), value_(nullptr) {}

  bool IsResolved() const { return value_ != nullptr; }

  void ResolveTo(std::unique_ptr<ConstantValue> value) {
    assert(value != nullptr);
    assert(!IsResolved() && "Constants should only be resolved once!");
    value_ = std::move(value);
  }

  const ConstantValue& Value() const {
    assert(IsResolved() && "Accessing the value of an unresolved Constant!");
    return *value_;
  }

  const Kind kind;

 protected:
  std::unique_ptr<ConstantValue> value_;
};

struct IdentifierConstant final : Constant {
  explicit IdentifierConstant(Name name) : Constant(Kind::kIdentifier), name(std::move(name)) {}

  const Name name;
};

struct LiteralConstant final : Constant {
  explicit LiteralConstant(std::unique_ptr<raw::Literal> literal)
      : Constant(Kind::kLiteral), literal(std::move(literal)) {}

  std::unique_ptr<raw::Literal> literal;
};

struct SynthesizedConstant final : Constant {
  explicit SynthesizedConstant(std::unique_ptr<ConstantValue> value)
      : Constant(Kind::kSynthesized) {
    ResolveTo(std::move(value));
  }
};

struct BinaryOperatorConstant final : Constant {
  enum struct Operator { kOr };

  explicit BinaryOperatorConstant(std::unique_ptr<Constant> left_operand,
                                  std::unique_ptr<Constant> right_operand, Operator op)
      : Constant(Kind::kBinaryOperator),
        left_operand(std::move(left_operand)),
        right_operand(std::move(right_operand)),
        op(op) {}

  const std::unique_ptr<Constant> left_operand;
  const std::unique_ptr<Constant> right_operand;
  const Operator op;
};

struct Decl {
  virtual ~Decl() {}

  enum struct Kind {
    kBits,
    kConst,
    kEnum,
    kProtocol,
    kService,
    kStruct,
    kTable,
    kUnion,
    kXUnion,
    kTypeAlias,
  };

  Decl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
      : kind(kind), attributes(std::move(attributes)), name(std::move(name)) {}

  const Kind kind;

  std::unique_ptr<raw::AttributeList> attributes;
  const Name name;

  bool HasAttribute(std::string_view name) const;
  std::string_view GetAttribute(std::string_view name) const;
  std::string GetName() const;

  bool compiling = false;
  bool compiled = false;
};

// An |Object| is anything that can be encoded in the FIDL wire format. Thus, all objects have
// information such as as their size, alignment, and depth (how many levels of sub-objects are
// contained within an object). See the FIDL wire format's definition of "object" for more details.
// TODO(fxb/37535): Remove this Object class, since it forms a third type hierarchy along with Type
// & Decl.
struct Object {
  virtual ~Object() = default;

  TypeShape typeshape(fidl::WireFormat wire_format) const { return TypeShape(*this, wire_format); }

  // |Visitor|, and the corresponding |Accept()| method below, enable the visitor pattern to be used
  // for derived classes of Object. See <https://en.wikipedia.org/wiki/Visitor_pattern> for
  // background on the visitor pattern. Versus a textbook visitor pattern:
  //
  // * Visitor enables a value to be returned to the caller of Accept(): Visitor's template type |T|
  //   is the type of the return value.
  //
  // * A Visitor's Visit() method returns an std::any. Visit() is responsible for returning a
  //   std::any with the correct type |T| for its contained value; otherwise, an any_cast exception
  //   will occur when the resulting std::any is any_casted back to |T| by Accept(). However, the
  //   client API that uses a visitor via Accept() will have guaranteed type safety.
  //
  // The use of std::any is an explicit design choice. It's possible to have a visitor
  // implementation that can completely retain type safety, but the use of std::any leads to a more
  // straightforward, ergonomic API than a solution involving heavy template metaprogramming.
  //
  // Implementation details: Visitor<T> is derived from VisitorAny, which achieves type-erasure via
  // std::any. Internally, only the type-erased VisitorAny class is used, along with a non-public
  // AcceptAny() method. The public Visitor<T> class and Accept<T> methods are small wrappers around
  // the internal type-erased versions. See
  // <https://eli.thegreenplace.net/2018/type-erasure-and-reification/> for a good introduction to
  // type erasure in C++.
  //
  // This struct is named Visitor since it's a visitor that cannot modify the Object, similarly
  // named to const_iterators.
  // TODO(fxb/37535): Refactor the visitor pattern here to be the simpler kind-enum + switch()
  // dispatch.
  template <typename T>
  struct Visitor;

  template <typename T>
  T Accept(Visitor<T>* visitor) const;

 protected:
  struct VisitorAny;
  virtual std::any AcceptAny(VisitorAny* visitor) const = 0;
};

struct TypeDecl : public Decl, public Object {
  TypeDecl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
      : Decl(kind, std::move(attributes), std::move(name)) {}

  bool recursive = false;
};

struct Type : public Object {
  virtual ~Type() {}

  enum struct Kind {
    kArray,
    kVector,
    kString,
    kHandle,
    kRequestHandle,
    kPrimitive,
    kIdentifier,
  };

  explicit Type(const Name& name, Kind kind, types::Nullability nullability)
      : name(name), kind(kind), nullability(nullability) {}

  const Name& name;
  const Kind kind;
  const types::Nullability nullability;

  // Comparison helper object.
  class Comparison {
   public:
    Comparison() = default;
    template <class T>
    Comparison Compare(const T& a, const T& b) const {
      if (result_ != 0)
        return Comparison(result_);
      if (a < b)
        return Comparison(-1);
      if (b < a)
        return Comparison(1);
      return Comparison(0);
    }

    bool IsLessThan() const { return result_ < 0; }

   private:
    Comparison(int result) : result_(result) {}

    const int result_ = 0;
  };

  bool operator<(const Type& other) const {
    if (kind != other.kind)
      return kind < other.kind;
    return Compare(other).IsLessThan();
  }

  // Compare this object against 'other'.
  // It's guaranteed that this->kind == other.kind.
  // Return <0 if *this < other, ==0 if *this == other, and >0 if *this > other.
  // Derived types should override this, but also call this implementation.
  virtual Comparison Compare(const Type& other) const {
    assert(kind == other.kind);
    return Comparison().Compare(nullability, other.nullability);
  }
};

struct ArrayType final : public Type {
  ArrayType(const Name& name, const Type* element_type, const Size* element_count)
      : Type(name, Kind::kArray, types::Nullability::kNonnullable),
        element_type(element_type),
        element_count(element_count) {}

  const Type* element_type;
  const Size* element_count;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const ArrayType&>(other);
    return Type::Compare(o)
        .Compare(element_count->value, o.element_count->value)
        .Compare(*element_type, *o.element_type);
  }
};

struct VectorType final : public Type {
  VectorType(const Name& name, const Type* element_type, const Size* element_count,
             types::Nullability nullability)
      : Type(name, Kind::kVector, nullability),
        element_type(element_type),
        element_count(element_count) {}

  const Type* element_type;
  const Size* element_count;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const VectorType&>(other);
    return Type::Compare(o)
        .Compare(element_count->value, o.element_count->value)
        .Compare(*element_type, *o.element_type);
  }
};

struct StringType final : public Type {
  StringType(const Name& name, const Size* max_size, types::Nullability nullability)
      : Type(name, Kind::kString, nullability), max_size(max_size) {}

  const Size* max_size;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const StringType&>(other);
    return Type::Compare(o).Compare(max_size->value, o.max_size->value);
  }
};

struct HandleType final : public Type {
  HandleType(const Name& name, types::HandleSubtype subtype, types::Nullability nullability)
      : Type(name, Kind::kHandle, nullability), subtype(subtype) {}

  const types::HandleSubtype subtype;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = *static_cast<const HandleType*>(&other);
    return Type::Compare(o).Compare(subtype, o.subtype);
  }
};

struct PrimitiveType final : public Type {
  explicit PrimitiveType(const Name& name, types::PrimitiveSubtype subtype)
      : Type(name, Kind::kPrimitive, types::Nullability::kNonnullable), subtype(subtype) {}

  types::PrimitiveSubtype subtype;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const PrimitiveType&>(other);
    return Type::Compare(o).Compare(subtype, o.subtype);
  }

 private:
  static uint32_t SubtypeSize(types::PrimitiveSubtype subtype);
};

struct IdentifierType final : public Type {
  IdentifierType(const Name& name, types::Nullability nullability, const TypeDecl* type_decl)
      : Type(name, Kind::kIdentifier, nullability), type_decl(type_decl) {}

  const TypeDecl* type_decl;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const IdentifierType&>(other);
    return Type::Compare(o).Compare(name, o.name);
  }
};

struct RequestHandleType final : public Type {
  RequestHandleType(const Name& name, const IdentifierType* protocol_type,
                    types::Nullability nullability)
      : Type(name, Kind::kRequestHandle, nullability), protocol_type(protocol_type) {}

  const IdentifierType* protocol_type;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const RequestHandleType&>(other);
    return Type::Compare(o).Compare(*protocol_type, *o.protocol_type);
  }
};

struct TypeAlias;

struct TypeConstructor final {
  struct FromTypeAlias {
    FromTypeAlias(const TypeAlias* decl, const Type* maybe_arg_type, const Size* maybe_size,
                  types::Nullability nullability) noexcept
        : decl(decl),
          maybe_arg_type(maybe_arg_type),
          maybe_size(maybe_size),
          nullability(nullability) {}
    const TypeAlias* decl;
    const Type* maybe_arg_type;
    const Size* maybe_size;
    // TODO(pascallouis): Make this const.
    types::Nullability nullability;
  };

  TypeConstructor(Name name, std::unique_ptr<TypeConstructor> maybe_arg_type_ctor,
                  std::optional<types::HandleSubtype> handle_subtype,
                  std::unique_ptr<Constant> maybe_size, types::Nullability nullability)
      : name(std::move(name)),
        maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
        handle_subtype(handle_subtype),
        maybe_size(std::move(maybe_size)),
        nullability(nullability) {}

  // Returns a type constructor for the size type (used for bounds).
  static std::unique_ptr<TypeConstructor> CreateSizeType();

  // Set during construction.
  const Name name;
  const std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
  const std::optional<types::HandleSubtype> handle_subtype;
  const std::unique_ptr<Constant> maybe_size;
  const types::Nullability nullability;

  // Set during compilation.
  bool compiling = false;
  bool compiled = false;
  const Type* type = nullptr;
  std::optional<FromTypeAlias> from_type_alias;
};

struct Using final {
  Using(Name name, const PrimitiveType* type) : name(std::move(name)), type(type) {}

  const Name name;
  const PrimitiveType* type;
};

// Const represents the _declaration_ of a constant. (For the _use_, see
// Constant. For the _value_, see ConstantValue.) A Const consists of a
// left-hand-side Name (found in Decl) and a right-hand-side Constant.
struct Const final : public Decl {
  Const(std::unique_ptr<raw::AttributeList> attributes, Name name,
        std::unique_ptr<TypeConstructor> type_ctor, std::unique_ptr<Constant> value)
      : Decl(Kind::kConst, std::move(attributes), std::move(name)),
        type_ctor(std::move(type_ctor)),
        value(std::move(value)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Constant> value;
};

struct Enum final : public TypeDecl {
  struct Member {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<raw::AttributeList> attributes)
        : name(name), value(std::move(value)), attributes(std::move(attributes)) {}
    SourceSpan name;
    std::unique_ptr<Constant> value;
    std::unique_ptr<raw::AttributeList> attributes;
  };

  Enum(std::unique_ptr<raw::AttributeList> attributes, Name name,
       std::unique_ptr<TypeConstructor> subtype_ctor, std::vector<Member> members,
       types::Strictness strictness)
      : TypeDecl(Kind::kEnum, std::move(attributes), std::move(name)),
        subtype_ctor(std::move(subtype_ctor)),
        members(std::move(members)),
        strictness(strictness) {}

  // Set during construction.
  std::unique_ptr<TypeConstructor> subtype_ctor;
  std::vector<Member> members;
  const types::Strictness strictness;

  std::any AcceptAny(VisitorAny* visitor) const override;

  // Set during compilation.
  const PrimitiveType* type = nullptr;
};

struct Bits final : public TypeDecl {
  struct Member {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<raw::AttributeList> attributes)
        : name(name), value(std::move(value)), attributes(std::move(attributes)) {}
    SourceSpan name;
    std::unique_ptr<Constant> value;
    std::unique_ptr<raw::AttributeList> attributes;
  };

  Bits(std::unique_ptr<raw::AttributeList> attributes, Name name,
       std::unique_ptr<TypeConstructor> subtype_ctor, std::vector<Member> members,
       types::Strictness strictness)
      : TypeDecl(Kind::kBits, std::move(attributes), std::move(name)),
        subtype_ctor(std::move(subtype_ctor)),
        members(std::move(members)),
        strictness(strictness) {}

  // Set during construction.
  std::unique_ptr<TypeConstructor> subtype_ctor;
  std::vector<Member> members;
  const types::Strictness strictness;

  std::any AcceptAny(VisitorAny* visitor) const override;

  // Set during compilation.
  uint64_t mask = 0;
};

struct Service final : public TypeDecl {
  struct Member {
    Member(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
           std::unique_ptr<raw::AttributeList> attributes)
        : type_ctor(std::move(type_ctor)),
          name(std::move(name)),
          attributes(std::move(attributes)) {}

    std::unique_ptr<TypeConstructor> type_ctor;
    SourceSpan name;
    std::unique_ptr<raw::AttributeList> attributes;
  };

  Service(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members)
      : TypeDecl(Kind::kService, std::move(attributes), std::move(name)),
        members(std::move(members)) {}

  std::any AcceptAny(VisitorAny* visitor) const override;

  std::vector<Member> members;
};

struct Struct;

// Historically, StructMember was a nested class inside Struct named Struct::Member. However, this
// was made a top-level class since it's not possible to forward-declare nested classes in C++. For
// backward-compatibility, Struct::Member is now an alias for this top-level StructMember.
// TODO(fxb/37535): Move this to a nested class inside Struct.
struct StructMember : public Object {
  StructMember(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<raw::AttributeList> attributes)
      : type_ctor(std::move(type_ctor)),
        name(std::move(name)),
        maybe_default_value(std::move(maybe_default_value)),
        attributes(std::move(attributes)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<Constant> maybe_default_value;
  std::unique_ptr<raw::AttributeList> attributes;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  const Struct* parent = nullptr;
};

struct Struct final : public TypeDecl {
  using Member = StructMember;

  Struct(std::unique_ptr<raw::AttributeList> attributes, Name name,
         std::vector<Member> unparented_members, bool is_request_or_response = false)
      : TypeDecl(Kind::kStruct, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)),
        is_request_or_response(is_request_or_response) {
    for (auto& member : members) {
      member.parent = this;
    }
  }

  std::vector<Member> members;

  // This is true iff this struct is a method request/response in a transaction header.
  const bool is_request_or_response;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Table;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Table::Member.
struct TableMemberUsed : public Object {
  TableMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
                  std::unique_ptr<Constant> maybe_default_value,
                  std::unique_ptr<raw::AttributeList> attributes)
      : type_ctor(std::move(type_ctor)),
        name(std::move(name)),
        maybe_default_value(std::move(maybe_default_value)),
        attributes(std::move(attributes)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<Constant> maybe_default_value;
  std::unique_ptr<raw::AttributeList> attributes;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Table.
struct TableMember : public Object {
  using Used = TableMemberUsed;

  TableMember(std::unique_ptr<raw::Ordinal32> ordinal, std::unique_ptr<TypeConstructor> type,
              SourceSpan name, std::unique_ptr<Constant> maybe_default_value,
              std::unique_ptr<raw::AttributeList> attributes)
      : ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type), std::move(name),
                                          std::move(maybe_default_value), std::move(attributes))) {}
  TableMember(std::unique_ptr<raw::Ordinal32> ordinal, SourceSpan span)
      : ordinal(std::move(ordinal)), span(span) {}

  std::unique_ptr<raw::Ordinal32> ordinal;

  // The span for reserved table members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Table final : public TypeDecl {
  using Member = TableMember;

  Table(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members,
        types::Strictness strictness)
      : TypeDecl(Kind::kTable, std::move(attributes), std::move(name)),
        members(std::move(members)),
        strictness(strictness) {}

  std::vector<Member> members;
  const types::Strictness strictness;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Union;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Union.
struct UnionMemberUsed : public Object {
  UnionMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
                  std::unique_ptr<raw::AttributeList> attributes)
      : type_ctor(std::move(type_ctor)), name(name), attributes(std::move(attributes)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<raw::AttributeList> attributes;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  const Union* parent = nullptr;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Union.
struct UnionMember : public Object {
  using Used = UnionMemberUsed;

  UnionMember(std::unique_ptr<raw::Ordinal32> xunion_ordinal,
              std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
              std::unique_ptr<raw::AttributeList> attributes)
      : xunion_ordinal(std::move(xunion_ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type_ctor), name, std::move(attributes))) {}
  UnionMember(std::unique_ptr<raw::Ordinal32> xunion_ordinal, SourceSpan span)
      : xunion_ordinal(std::move(xunion_ordinal)), span(span) {}

  std::unique_ptr<raw::Ordinal32> xunion_ordinal;

  // The span for reserved members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Union final : public TypeDecl {
  using Member = UnionMember;

  Union(std::unique_ptr<raw::AttributeList> attributes, Name name,
        std::vector<Member> unparented_members)
      : TypeDecl(Kind::kUnion, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)) {
    for (auto& member : members) {
      if (member.maybe_used) {
        member.maybe_used->parent = this;
      }
    }
  }

  std::vector<Member> members;

  // Returns references to union members sorted by their xunion_ordinal.
  std::vector<std::reference_wrapper<const Member>> MembersSortedByXUnionOrdinal() const;

  std::any AcceptAny(VisitorAny* visitor) const override;

  // Returns the offset from the start of union where the union data resides. (Either 4, or 8.)
  // This may only be queried for the old wire format (`WireFormat::kOld`).
  uint32_t DataOffset(WireFormat wire_format) const;
};

struct XUnion;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Union.
struct XUnionMemberUsed : public Object {
  XUnionMemberUsed(std::unique_ptr<raw::Ordinal32> hashed_ordinal,
                   std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
                   std::unique_ptr<raw::AttributeList> attributes)
      : hashed_ordinal(std::move(hashed_ordinal)),
        type_ctor(std::move(type_ctor)),
        name(name),
        attributes(std::move(attributes)) {}
  std::unique_ptr<raw::Ordinal32> hashed_ordinal;
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<raw::AttributeList> attributes;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  const XUnion* parent = nullptr;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxb/37535): Move this to a nested class inside Union.
struct XUnionMember : public Object {
  using Used = XUnionMemberUsed;

  XUnionMember(std::unique_ptr<raw::Ordinal32> explicit_ordinal,
               std::unique_ptr<raw::Ordinal32> hashed_ordinal,
               std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
               std::unique_ptr<raw::AttributeList> attributes, bool using_explicit_ordinal = false)
      : explicit_ordinal(std::move(explicit_ordinal)),
        maybe_used(std::make_unique<Used>(std::move(hashed_ordinal), std::move(type_ctor), name,
                                          std::move(attributes))),
        using_explicit_ordinal(using_explicit_ordinal) {}
  XUnionMember(std::unique_ptr<raw::Ordinal32> explicit_ordinal, SourceSpan span)
      : explicit_ordinal(std::move(explicit_ordinal)), span(span) {}

  std::unique_ptr<raw::Ordinal32> explicit_ordinal;

  // The span for reserved members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  // Indicates whether this xunion member should be writing explicit ordinals
  // on the wire. The xunions for which this is true are tracked in the explicit_ordinal_xunions
  // map in flat_ast.cc
  bool using_explicit_ordinal;

  std::any AcceptAny(VisitorAny* visitor) const override;

  const std::unique_ptr<raw::Ordinal32>& write_ordinal() const {
    if (!maybe_used) {
      return explicit_ordinal;
    }
    return using_explicit_ordinal ? explicit_ordinal : maybe_used->hashed_ordinal;
  }
};

struct XUnion final : public TypeDecl {
  using Member = XUnionMember;

  XUnion(std::unique_ptr<raw::AttributeList> attributes, Name name,
         std::vector<Member> unparented_members, types::Strictness strictness)
      : TypeDecl(Kind::kXUnion, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)),
        strictness(strictness) {
    for (auto& member : members) {
      if (member.maybe_used) {
        member.maybe_used->parent = this;
      }
    }
  }

  std::vector<Member> members;
  const types::Strictness strictness;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Protocol final : public TypeDecl {
  struct Method {
    Method(Method&&) = default;
    Method& operator=(Method&&) = default;

    Method(std::unique_ptr<raw::AttributeList> attributes,
           std::unique_ptr<raw::Ordinal32> generated_ordinal32,
           std::unique_ptr<raw::Ordinal64> generated_ordinal64, SourceSpan name,
           Struct* maybe_request, Struct* maybe_response)
        : attributes(std::move(attributes)),
          generated_ordinal32(std::move(generated_ordinal32)),
          generated_ordinal64(std::move(generated_ordinal64)),
          name(std::move(name)),
          maybe_request(maybe_request),
          maybe_response(maybe_response) {
      assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
    }

    std::unique_ptr<raw::AttributeList> attributes;
    // To be removed when FIDL-524 has completed.
    std::unique_ptr<raw::Ordinal32> generated_ordinal32;
    std::unique_ptr<raw::Ordinal64> generated_ordinal64;
    SourceSpan name;
    Struct* maybe_request;
    Struct* maybe_response;
    // This is set to the |Protocol| instance that owns this |Method|,
    // when the |Protocol| is constructed.
    Protocol* owning_protocol = nullptr;
  };

  // Used to keep track of a all methods (i.e. including composed methods).
  // Method pointers here are set after composed_protocols are compiled, and
  // are owned by the corresponding composed_protocols.
  struct MethodWithInfo {
    MethodWithInfo(const Method* method, bool is_composed)
        : method(method), is_composed(is_composed) {}
    const Method* method;
    const bool is_composed;
  };

  Protocol(std::unique_ptr<raw::AttributeList> attributes, Name name,
           std::set<Name> composed_protocols, std::vector<Method> methods)
      : TypeDecl(Kind::kProtocol, std::move(attributes), std::move(name)),
        composed_protocols(std::move(composed_protocols)),
        methods(std::move(methods)) {
    for (auto& method : this->methods) {
      method.owning_protocol = this;
    }
  }

  std::set<Name> composed_protocols;
  std::vector<Method> methods;
  std::vector<MethodWithInfo> all_methods;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct TypeAlias final : public Decl {
  TypeAlias(std::unique_ptr<raw::AttributeList> attributes, Name name,
            std::unique_ptr<TypeConstructor> partial_type_ctor)
      : Decl(Kind::kTypeAlias, std::move(attributes), std::move(name)),
        partial_type_ctor(std::move(partial_type_ctor)) {}

  const std::unique_ptr<TypeConstructor> partial_type_ctor;
};

class TypeTemplate {
 public:
  TypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter)
      : typespace_(typespace), name_(std::move(name)), error_reporter_(error_reporter) {}

  TypeTemplate(TypeTemplate&& type_template) = default;

  virtual ~TypeTemplate() = default;

  const Name* name() const { return &name_; }

  virtual bool Create(const std::optional<SourceSpan>& span, const Type* arg_type,
                      const std::optional<types::HandleSubtype>& handle_subtype, const Size* size,
                      types::Nullability nullability, std::unique_ptr<Type>* out_type,
                      std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const = 0;

 protected:
  bool MustBeParameterized(const std::optional<SourceSpan>& span) const {
    return Fail(span, "must be parametrized");
  }
  bool MustHaveSize(const std::optional<SourceSpan>& span) const {
    return Fail(span, "must have size");
  }
  bool MustHaveNonZeroSize(const std::optional<SourceSpan>& span) const {
    return Fail(span, "must have non-zero size");
  }
  bool CannotBeParameterized(const std::optional<SourceSpan>& span) const {
    return Fail(span, "cannot be parametrized");
  }
  bool CannotHaveSize(const std::optional<SourceSpan>& span) const {
    return Fail(span, "cannot have size");
  }
  bool CannotBeNullable(const std::optional<SourceSpan>& span) const {
    return Fail(span, "cannot be nullable");
  }
  bool Fail(const std::optional<SourceSpan>& span, const std::string& content) const;

  Typespace* typespace_;

  Name name_;

 private:
  ErrorReporter* error_reporter_;
};

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
class Typespace {
 public:
  explicit Typespace(ErrorReporter* error_reporter) : error_reporter_(error_reporter) {}

  bool Create(const flat::Name& name, const Type* arg_type,
              const std::optional<types::HandleSubtype>& handle_subtype, const Size* size,
              types::Nullability nullability, const Type** out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias);

  void AddTemplate(std::unique_ptr<TypeTemplate> type_template);

  // RootTypes creates a instance with all primitive types. It is meant to be
  // used as the top-level types lookup mechanism, providing definitional
  // meaning to names such as `int64`, or `bool`.
  static Typespace RootTypes(ErrorReporter* error_reporter);

 private:
  friend class TypeAliasTypeTemplate;

  bool CreateNotOwned(const flat::Name& name, const Type* arg_type,
                      const std::optional<types::HandleSubtype>& handle_subtype, const Size* size,
                      types::Nullability nullability, std::unique_ptr<Type>* out_type,
                      std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias);
  const TypeTemplate* LookupTemplate(const flat::Name& name) const;

  std::map<const flat::Name*, std::unique_ptr<TypeTemplate>, PtrCompare<Name>> templates_;
  std::vector<std::unique_ptr<Type>> types_;

  ErrorReporter* error_reporter_;
};

// AttributeSchema defines a schema for attributes. This includes:
// - The allowed placement of an attribute (e.g. on a method, on a struct
//   declaration);
// - The allowed values which an attribute can take.
// For attributes which may be placed on declarations (e.g. protocol, struct,
// union, table), a schema may additionally include:
// - A constraint which must be met by the declaration.
class AttributeSchema {
 public:
  using Constraint = fit::function<bool(ErrorReporter* error_reporter,
                                        const raw::Attribute& attribute, const Decl* decl)>;

  // Placement indicates the placement of an attribute, e.g. whether an
  // attribute is placed on an enum declaration, method, or union
  // member.
  enum class Placement {
    kBitsDecl,
    kBitsMember,
    kConstDecl,
    kEnumDecl,
    kEnumMember,
    kProtocolDecl,
    kLibrary,
    kMethod,
    kServiceDecl,
    kServiceMember,
    kStructDecl,
    kStructMember,
    kTableDecl,
    kTableMember,
    kTypeAliasDecl,
    kUnionDecl,
    kUnionMember,
    kXUnionDecl,
    kXUnionMember,
  };

  AttributeSchema(const std::set<Placement>& allowed_placements,
                  const std::set<std::string> allowed_values,
                  Constraint constraint = NoOpConstraint);

  AttributeSchema(AttributeSchema&& schema) = default;

  void ValidatePlacement(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                         Placement placement) const;

  void ValidateValue(ErrorReporter* error_reporter, const raw::Attribute& attribute) const;

  void ValidateConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                          const Decl* decl) const;

 private:
  static bool NoOpConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                             const Decl* decl) {
    return true;
  }

  std::set<Placement> allowed_placements_;
  std::set<std::string> allowed_values_;
  Constraint constraint_;
};

class Libraries {
 public:
  Libraries();

  // Insert |library|.
  bool Insert(std::unique_ptr<Library> library);

  // Lookup a library by its |library_name|.
  bool Lookup(const std::vector<std::string_view>& library_name, Library** out_library) const;

  void AddAttributeSchema(const std::string& name, AttributeSchema schema) {
    [[maybe_unused]] auto iter = attribute_schemas_.emplace(name, std::move(schema));
    assert(iter.second && "do not add schemas twice");
  }

  const AttributeSchema* RetrieveAttributeSchema(ErrorReporter* error_reporter,
                                                 const raw::Attribute& attribute) const;

  std::set<std::vector<std::string_view>> Unused(const Library* target_library) const;

 private:
  std::map<std::vector<std::string_view>, std::unique_ptr<Library>> all_libraries_;
  std::map<std::string, AttributeSchema> attribute_schemas_;
};

class Dependencies {
 public:
  // Register a dependency to a library. The newly recorded dependent library
  // will be referenced by its name, and may also be optionally be referenced
  // by an alias.
  bool Register(const SourceSpan& span, std::string_view filename, Library* dep_library,
                const std::unique_ptr<raw::Identifier>& maybe_alias);

  // Returns true if this dependency set contains a library with the given name and filename.
  bool Contains(std::string_view filename, const std::vector<std::string_view>& name);

  // Looks up a dependent library by |filename| and |name|, and marks it as
  // used.
  bool LookupAndUse(std::string_view filename, const std::vector<std::string_view>& name,
                    Library** out_library);

  // VerifyAllDependenciesWereUsed verifies that all regisered dependencies
  // were used, i.e. at least one lookup was made to retrieve them.
  // Reports errors directly, and returns true if one error or more was
  // reported.
  bool VerifyAllDependenciesWereUsed(const Library& for_library, ErrorReporter* error_reporter);

  const std::set<Library*>& dependencies() const { return dependencies_aggregate_; }

 private:
  struct LibraryRef {
    LibraryRef(const SourceSpan span, Library* library) : span_(span), library_(library) {}

    const SourceSpan span_;
    Library* library_;
    bool used_ = false;
  };

  bool InsertByName(std::string_view filename, const std::vector<std::string_view>& name,
                    LibraryRef* ref);

  using ByName = std::map<std::vector<std::string_view>, LibraryRef*>;
  using ByFilename = std::map<std::string, std::unique_ptr<ByName>>;

  std::vector<std::unique_ptr<LibraryRef>> refs_;
  ByFilename dependencies_;
  std::set<Library*> dependencies_aggregate_;
};

class Library {
 public:
  Library(const Libraries* all_libraries, ErrorReporter* error_reporter, Typespace* typespace)
      : all_libraries_(all_libraries), error_reporter_(error_reporter), typespace_(typespace) {}

  bool ConsumeFile(std::unique_ptr<raw::File> file);
  bool Compile();

  const std::vector<std::string_view>& name() const { return library_name_; }
  const std::vector<std::string>& errors() const { return error_reporter_->errors(); }
  const raw::AttributeList* attributes() const { return attributes_.get(); }

 private:
  bool Fail(std::string_view message);
  bool Fail(const std::optional<SourceSpan>& span, std::string_view message);
  bool Fail(const Name& name, std::string_view message) { return Fail(name.span(), message); }
  bool Fail(const Decl& decl, std::string_view message) { return Fail(decl.name, message); }

  void ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                   const raw::AttributeList* attributes);
  void ValidateAttributesConstraints(const Decl* decl, const raw::AttributeList* attributes);

  // TODO(FIDL-596): Rationalize the use of names. Here, a simple name is
  // one that is not scoped, it is just text. An anonymous name is one that
  // is guaranteed to be unique within the library, and a derived name is one
  // that is library scoped but derived from the concatenated components using
  // underscores as delimiters.
  SourceSpan GeneratedSimpleName(const std::string& name);
  Name NextAnonymousName();
  Name DerivedName(const std::vector<std::string_view>& components);

  // Attempts to compile a compound identifier, and resolve it to a name
  // within the context of a library. On success, the name is returned.
  // On failure, no name is returned, and a failure is emitted, i.e. the
  // caller is not responsible for reporting the resolution error.
  std::optional<Name> CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier);
  bool RegisterDecl(std::unique_ptr<Decl> decl);

  bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceSpan span,
                       std::unique_ptr<Constant>* out_constant);
  bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor, SourceSpan span,
                              std::unique_ptr<TypeConstructor>* out_type);

  bool ConsumeUsing(std::unique_ptr<raw::Using> using_directive);
  bool ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive);
  bool ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration);
  bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
  bool ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
  bool ConsumeProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration);
  bool ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                            bool anonymous, Struct** out_struct_decl);
  bool CreateMethodResult(const Name& protocol_name, raw::ProtocolMethod* method,
                          Struct* in_response, Struct** out_response);
  bool ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl);
  bool ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
  bool ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration);
  bool ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);
  bool ConsumeXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> xunion_declaration);

  bool TypeCanBeConst(const Type* type);
  const Type* TypeResolve(const Type* type);
  bool TypeIsConvertibleTo(const Type* from_type, const Type* to_type);
  std::unique_ptr<TypeConstructor> IdentifierTypeForDecl(const Decl* decl,
                                                         types::Nullability nullability);

  bool AddConstantDependencies(const Constant* constant, std::set<Decl*>* out_edges);
  bool DeclDependencies(Decl* decl, std::set<Decl*>* out_edges);

  bool SortDeclarations();

  bool CompileBits(Bits* bits_declaration);
  bool CompileConst(Const* const_declaration);
  bool CompileEnum(Enum* enum_declaration);
  bool CompileProtocol(Protocol* protocol_declaration);
  bool CompileService(Service* service_decl);
  bool CompileStruct(Struct* struct_declaration);
  bool CompileTable(Table* table_declaration);
  bool CompileUnion(Union* union_declaration);
  bool CompileXUnion(XUnion* xunion_declaration);
  bool CompileTypeAlias(TypeAlias* type_alias);

  // Compiling a type validates the type: in particular, we validate that optional identifier types
  // refer to things that can in fact be nullable (ie not enums).
  bool CompileTypeConstructor(TypeConstructor* type);

  ConstantValue::Kind ConstantValuePrimitiveKind(const types::PrimitiveSubtype primitive_subtype);
  bool ResolveSizeBound(TypeConstructor* type_ctor, const Size** out_size);
  bool ResolveOrOperatorConstant(Constant* constant, const Type* type,
                                 const ConstantValue& left_operand,
                                 const ConstantValue& right_operand);
  bool ResolveConstant(Constant* constant, const Type* type);
  bool ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type);
  bool ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type);

  // Validates a single member of a bits or enum. On failure,
  // returns false and places an error message in the out parameter.
  template <typename MemberType>
  using MemberValidator = fit::function<bool(const MemberType& member, std::string* out_error)>;
  template <typename DeclType, typename MemberType>
  bool ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator);
  template <typename MemberType>
  bool ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask);
  template <typename MemberType>
  bool ValidateEnumMembers(Enum* enum_decl);

  bool VerifyDeclAttributes(Decl* decl);

 public:
  bool CompileDecl(Decl* decl);

  // Returns nullptr when the |name| cannot be resolved to a
  // Name. Otherwise it returns the declaration.
  Decl* LookupDeclByName(const Name& name) const;

  template <typename NumericType>
  bool ParseNumericLiteral(const raw::NumericLiteral* literal, NumericType* out_value) const;

  bool HasAttribute(std::string_view name) const;

  const std::set<Library*>& dependencies() const;

  std::vector<std::string_view> library_name_;

  std::vector<std::unique_ptr<Bits>> bits_declarations_;
  std::vector<std::unique_ptr<Const>> const_declarations_;
  std::vector<std::unique_ptr<Enum>> enum_declarations_;
  std::vector<std::unique_ptr<Protocol>> protocol_declarations_;
  std::vector<std::unique_ptr<Service>> service_declarations_;
  std::vector<std::unique_ptr<Struct>> struct_declarations_;
  std::vector<std::unique_ptr<Table>> table_declarations_;
  std::vector<std::unique_ptr<Union>> union_declarations_;
  std::vector<std::unique_ptr<XUnion>> xunion_declarations_;
  std::vector<std::unique_ptr<TypeAlias>> type_alias_declarations_;

  // All Decl pointers here are non-null and are owned by the
  // various foo_declarations_.
  std::vector<Decl*> declaration_order_;

 private:
  // TODO(FIDL-389): Remove when canonicalizing types.
  const Name kSizeTypeName = Name(nullptr, "uint32");
  const PrimitiveType kSizeType = PrimitiveType(kSizeTypeName, types::PrimitiveSubtype::kUint32);

  std::unique_ptr<raw::AttributeList> attributes_;

  Dependencies dependencies_;
  const Libraries* all_libraries_;

  // All Name, Constant, Using, and Decl pointers here are non-null and are
  // owned by the various foo_declarations_.
  std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;

  ErrorReporter* error_reporter_;
  Typespace* typespace_;

  uint32_t anon_counter_ = 0;

  VirtualSourceFile generated_source_file_{"generated"};
};

// See the comment on Object::Visitor<T> for more details.
struct Object::VisitorAny {
  virtual std::any Visit(const ArrayType&) = 0;
  virtual std::any Visit(const VectorType&) = 0;
  virtual std::any Visit(const StringType&) = 0;
  virtual std::any Visit(const HandleType&) = 0;
  virtual std::any Visit(const PrimitiveType&) = 0;
  virtual std::any Visit(const IdentifierType&) = 0;
  virtual std::any Visit(const RequestHandleType&) = 0;
  virtual std::any Visit(const Enum&) = 0;
  virtual std::any Visit(const Bits&) = 0;
  virtual std::any Visit(const Service&) = 0;
  virtual std::any Visit(const Struct&) = 0;
  virtual std::any Visit(const Struct::Member&) = 0;
  virtual std::any Visit(const Table&) = 0;
  virtual std::any Visit(const Table::Member&) = 0;
  virtual std::any Visit(const Table::Member::Used&) = 0;
  virtual std::any Visit(const Union&) = 0;
  virtual std::any Visit(const Union::Member&) = 0;
  virtual std::any Visit(const Union::Member::Used&) = 0;
  virtual std::any Visit(const XUnion&) = 0;
  virtual std::any Visit(const XUnion::Member&) = 0;
  virtual std::any Visit(const XUnion::Member::Used&) = 0;
  virtual std::any Visit(const Protocol&) = 0;
};

// This Visitor<T> class is useful so that Object.Accept() can enforce that its return type
// matches the template type of Visitor. See the comment on Object::Visitor<T> for more
// details.
template <typename T>
struct Object::Visitor : public VisitorAny {};

template <typename T>
T Object::Accept(Visitor<T>* visitor) const {
  return std::any_cast<T>(AcceptAny(visitor));
}

inline std::any ArrayType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any VectorType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any StringType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any HandleType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any PrimitiveType::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any IdentifierType::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any RequestHandleType::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Enum::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Bits::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Service::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Struct::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Struct::Member::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Table::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Table::Member::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Table::Member::Used::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Union::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any Union::Member::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Union::Member::Used::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any XUnion::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

inline std::any XUnion::Member::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any XUnion::Member::Used::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

inline std::any Protocol::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

}  // namespace flat
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_AST_H_
