// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPES_H_

#include "name.h"
#include "object.h"
#include "values.h"

namespace fidl {
namespace flat {

struct Decl;
struct TypeDecl;
struct Struct;
class LibraryMediator;
struct LayoutInvocation;
struct TypeConstraints;
struct Resource;
class TypeTemplate;

struct Type : public Object {
  virtual ~Type() {}

  enum struct Kind {
    kArray,
    kBox,
    kVector,
    kString,
    kHandle,
    kTransportSide,
    kPrimitive,
    kIdentifier,
  };

  explicit Type(const Name& name, Kind kind, types::Nullability nullability)
      : name(name), kind(kind), nullability(nullability) {}

  const Name name;
  const Kind kind;
  // TODO(fxbug.dev/70186): This is temporarily not-const so that we can modify
  // any boxed structs' nullability to always be nullable, in order use
  // pre-existing "box <=> nullable struct" logic
  types::Nullability nullability;

  // Returns the nominal resourceness of the type per the FTP-057 definition.
  // For IdentifierType, can only be called after the Decl has been compiled.
  types::Resourceness Resourceness() const;

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

  // Apply the provided constraints to this type, returning the newly constrained
  // Type and recording the invocation inside resolved_args.
  // For types in the new syntax, we receive the unresolved TypeConstraints.
  // TODO(fxbug.dev/74193): We currently require a pointer to the calling TypeTemplate
  // for error reporting purposes, since all of the constraint related errors are
  // still tied to TypeTemplates. As we fully separate out the constraints and
  // layout parameter (TypeTemplate::Create) code, we'll be able to remove this
  // extraneous parameter.
  virtual bool ApplyConstraints(const flat::LibraryMediator& lib,
                                const TypeConstraints& constraints,
                                const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                                LayoutInvocation* out_params) const = 0;
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

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

struct VectorBaseType {
  // "vector based" types share common code for determining the size and nullability.
  // This method provides the resolved size and nullability, so that specific implementations
  // only need to worry about setting the element type on out_args.
  // We can't abstract away only the element type resolution process, because not
  // all vector based type templates return a VectorType (the exception being StringTypeTemplate).
  static bool ResolveSizeAndNullability(const LibraryMediator& lib,
                                        const TypeConstraints& constraints,
                                        const TypeTemplate* layout, LayoutInvocation* out_params);

  const static Size kMaxSize;
};

struct VectorType final : public Type, public VectorBaseType {
  VectorType(const Name& name, const Type* element_type)
      : Type(name, Kind::kVector, types::Nullability::kNonnullable),
        element_type(element_type),
        element_count(&kMaxSize) {}
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

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

struct StringType final : public Type, public VectorBaseType {
  explicit StringType(const Name& name)
      : Type(name, Kind::kString, types::Nullability::kNonnullable), max_size(&kMaxSize) {}
  StringType(const Name& name, const Size* max_size, types::Nullability nullability)
      : Type(name, Kind::kString, nullability), max_size(max_size) {}

  const Size* max_size;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const StringType&>(other);
    return Type::Compare(o).Compare(max_size->value, o.max_size->value);
  }

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

struct HandleType final : public Type {
  HandleType(const Name& name, Resource* resource_decl)
      : Type(name, Kind::kHandle, types::Nullability::kNonnullable),
        resource_decl(resource_decl),
        // TODO(fxbug.dev/64629): When we are ready to create handle types, we
        // should have an object type (and/or subtype) determined and not require
        // these hardcoded defaults.
        // We need to allow setting a default obj_type in resource_definition
        // declarations rather than hard-coding.
        obj_type(static_cast<uint32_t>(types::HandleSubtype::kHandle)),
        subtype(types::HandleSubtype::kHandle),
        rights(&kSameRights) {}

  HandleType(const Name& name, Resource* resource_decl, uint32_t obj_type,
             types::HandleSubtype subtype, const HandleRights* rights,
             types::Nullability nullability)
      : Type(name, Kind::kHandle, nullability),
        resource_decl(resource_decl),
        obj_type(obj_type),
        subtype(subtype),
        rights(rights) {}

  Resource* resource_decl;
  const uint32_t obj_type;
  const types::HandleSubtype subtype;
  const HandleRights* rights;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& other_handle_type = *static_cast<const HandleType*>(&other);
    auto rights_val =
        static_cast<const flat::NumericConstantValue<types::RightsWrappedType>*>(rights);
    auto other_rights_val =
        static_cast<const flat::NumericConstantValue<types::RightsWrappedType>*>(
            other_handle_type.rights);
    return Type::Compare(other_handle_type)
        .Compare(subtype, other_handle_type.subtype)
        .Compare(*rights_val, *other_rights_val);
  }

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;

  const static HandleRights kSameRights;
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

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;

 private:
  static uint32_t SubtypeSize(types::PrimitiveSubtype subtype);
};

struct IdentifierType final : public Type {
  IdentifierType(const Name& name, const TypeDecl* type_decl)
      : IdentifierType(name, type_decl, types::Nullability::kNonnullable) {}
  IdentifierType(const Name& name, const TypeDecl* type_decl, types::Nullability nullability)
      : Type(name, Kind::kIdentifier, nullability), type_decl(type_decl) {}

  const TypeDecl* type_decl;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const IdentifierType&>(other);
    return Type::Compare(o).Compare(name, o.name);
  }

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

enum class TransportSide {
  kClient,
  kServer,
};

// TODO(fxbug.dev/43803) Add required and optional rights.
struct TransportSideType final : public Type {
  TransportSideType(const Name& name, TransportSide end)
      : TransportSideType(name, nullptr, types::Nullability::kNonnullable, end) {}
  TransportSideType(const Name& name, const Decl* protocol_decl, types::Nullability nullability,
                    TransportSide end)
      : Type(name, Kind::kTransportSide, nullability), protocol_decl(protocol_decl), end(end) {}

  const Decl* protocol_decl;
  const TransportSide end;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const TransportSideType&>(other);
    return Type::Compare(o)
        .Compare(name, o.name)
        .Compare(end, o.end)
        .Compare(protocol_decl, o.protocol_decl);
  }

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

struct BoxType final : public Type {
  BoxType(const Name& name, const Type* boxed_type)
      // Note that all boxes are implicitly nullable, so the value of the nullability
      // member here doesn't actually matter.
      : Type(name, Kind::kBox, types::Nullability::kNullable), boxed_type(boxed_type) {}

  const Type* boxed_type;

  std::any AcceptAny(VisitorAny* visitor) const override;

  Comparison Compare(const Type& other) const override {
    const auto& o = static_cast<const BoxType&>(other);
    return Type::Compare(o).Compare(name, o.name).Compare(boxed_type, o.boxed_type);
  }

  bool ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                        const flat::TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                        LayoutInvocation* out_params) const override;
};

}  // namespace flat
}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TYPES_H_
