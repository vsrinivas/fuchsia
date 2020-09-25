// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_TYPES_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_TYPES_H_

#include "name.h"
#include "object.h"
#include "values.h"

namespace fidl {
namespace flat {

struct TypeDecl;

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
  HandleType(const Name& name, types::HandleSubtype subtype, const Constant* rights,
             types::Nullability nullability)
      : Type(name, Kind::kHandle, nullability), subtype(subtype), rights(rights) {}

  const types::HandleSubtype subtype;
  const Constant* rights;

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

// TODO(fxbug.dev/43803) Add required and optional rights.
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

}  // namespace flat
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_TYPES_H_
