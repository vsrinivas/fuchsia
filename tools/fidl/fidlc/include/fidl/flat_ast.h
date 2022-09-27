// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#compilation
// for documentation

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_

#include <lib/fit/function.h>
#include <zircon/assert.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/flat/attributes.h"
#include "tools/fidl/fidlc/include/fidl/flat/name.h"
#include "tools/fidl/fidlc/include/fidl/flat/object.h"
#include "tools/fidl/fidlc/include/fidl/flat/traits.h"
#include "tools/fidl/fidlc/include/fidl/flat/types.h"
#include "tools/fidl/fidlc/include/fidl/flat/values.h"
#include "tools/fidl/fidlc/include/fidl/ordinals.h"
#include "tools/fidl/fidlc/include/fidl/type_shape.h"
#include "tools/fidl/fidlc/include/fidl/types.h"
#include "tools/fidl/fidlc/include/fidl/versioning_types.h"

namespace fidl {

class Reporter;
class VirtualSourceFile;

}  // namespace fidl

namespace fidl::raw {

class Identifier;
class Ordinal64;

}  // namespace fidl::raw

namespace fidl::flat {

struct Decl;
struct Library;
class AttributeSchema;
class Typespace;

bool HasSimpleLayout(const Decl* decl);

// This is needed (for now) to work around declaration order issues.
std::string LibraryName(const std::vector<std::string_view>& components,
                        std::string_view separator);

struct Element {
  enum struct Kind {
    kBits,
    kBitsMember,
    kBuiltin,
    kConst,
    kEnum,
    kEnumMember,
    kLibrary,
    kNewType,
    kProtocol,
    kProtocolCompose,
    kProtocolMethod,
    kResource,
    kResourceProperty,
    kService,
    kServiceMember,
    kStruct,
    kStructMember,
    kTable,
    kTableMember,
    kTypeAlias,
    kUnion,
    kUnionMember,
  };

  Element(const Element&) = delete;
  Element(Element&&) = default;
  Element& operator=(Element&&) = default;
  virtual ~Element() = default;

  Element(Kind kind, std::unique_ptr<AttributeList> attributes)
      : kind(kind), attributes(std::move(attributes)) {}

  // Returns true if this element is a decl.
  bool IsDecl() const;
  // Asserts that this element is a decl.
  Decl* AsDecl();

  // Returns true if this is an anonymous layout (i.e. a layout not
  // directly bound to a type declaration as in `type Foo = struct { ... };`).
  bool IsAnonymousLayout() const;

  Kind kind;
  std::unique_ptr<AttributeList> attributes;
  Availability availability;
};

struct Decl : public Element {
  enum struct Kind {
    kBits,
    kBuiltin,
    kConst,
    kEnum,
    kNewType,
    kProtocol,
    kResource,
    kService,
    kStruct,
    kTable,
    kTypeAlias,
    kUnion,
  };

  static Element::Kind ElementKind(Kind kind) {
    switch (kind) {
      case Kind::kBits:
        return Element::Kind::kBits;
      case Kind::kBuiltin:
        return Element::Kind::kBuiltin;
      case Kind::kConst:
        return Element::Kind::kConst;
      case Kind::kEnum:
        return Element::Kind::kEnum;
      case Kind::kNewType:
        return Element::Kind::kNewType;
      case Kind::kProtocol:
        return Element::Kind::kProtocol;
      case Kind::kResource:
        return Element::Kind::kResource;
      case Kind::kService:
        return Element::Kind::kService;
      case Kind::kStruct:
        return Element::Kind::kStruct;
      case Kind::kTable:
        return Element::Kind::kTable;
      case Kind::kTypeAlias:
        return Element::Kind::kTypeAlias;
      case Kind::kUnion:
        return Element::Kind::kUnion;
    }
  }

  Decl(Kind kind, std::unique_ptr<AttributeList> attributes, Name name)
      : Element(ElementKind(kind), std::move(attributes)), kind(kind), name(std::move(name)) {}

  const Kind kind;
  const Name name;

  std::string GetName() const;

  // Runs a function on every member of the decl, if it has any. Note that
  // unlike Library::TraverseElements, it does not call `fn(this)`.
  void ForEachMember(const fit::function<void(Element*)>& fn);

  // Returns a clone of this decl for the given range, only including members
  // that intersect the range. Narrows the returned decl's availability, and its
  // members' availabilities, to the range.
  std::unique_ptr<Decl> Split(VersionRange range) const;

  bool compiling = false;
  bool compiled = false;

 private:
  // Helper to implement Split. Leaves the result's availability unset.
  virtual std::unique_ptr<Decl> SplitImpl(VersionRange range) const = 0;
};

struct Builtin : public Decl {
  enum struct Identity {
    // Layouts (primitive)
    kBool,
    kInt8,
    kInt16,
    kInt32,
    kInt64,
    kUint8,
    kZxUchar,
    kUint16,
    kUint32,
    kUint64,
    kZxUsize,
    kZxUintptr,
    kFloat32,
    kFloat64,
    // Layouts (other)
    kString,
    // Layouts (templated)
    kBox,
    kArray,
    kVector,
    kClientEnd,
    kServerEnd,
    // Layouts (aliases)
    kByte,
    // Layouts (internal)
    kTransportErr,
    // Constraints
    kOptional,
    kMax,
    // Constants
    kHead,
  };

  explicit Builtin(Identity id, Name name)
      : Decl(Decl::Kind::kBuiltin, std::make_unique<AttributeList>(), std::move(name)), id(id) {}

  const Identity id;

  // Return true if this decl is for an internal fidl type.
  bool IsInternal() const;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct TypeDecl : public Decl, public Object {
  TypeDecl(Kind kind, std::unique_ptr<AttributeList> attributes, Name name)
      : Decl(kind, std::move(attributes), std::move(name)) {}

  bool recursive = false;
};

struct TypeConstructor;
struct TypeAlias;
struct Protocol;

// This is a struct used to group together all data produced during compilation
// that might be used by consumers that are downstream from type compilation
// (e.g. typeshape code, declaration sorting, JSON generator), that can't be
// obtained by looking at a type constructor's Type.
// Unlike TypeConstructor::Type which will always refer to the fully resolved/
// concrete (and eventually, canonicalized) type that the type constructor
// resolves to, this struct stores data about the actual parameters on this
// type constructor used to produce the type.
// These fields should be set in the same place where the parameters actually get
// resolved, i.e. Create (for layout parameters) and ApplyConstraints (for type
// constraints)
struct LayoutInvocation {
  // set if this type constructor refers to a type alias
  const TypeAlias* from_type_alias = nullptr;

  // Parameter data below: if a foo_resolved form is set, then its corresponding
  // foo_raw form must be defined as well (and vice versa).

  // resolved form of this type constructor's arguments
  const Type* element_type_resolved = nullptr;
  const Size* size_resolved = nullptr;
  // This has no users, probably because it's missing in the JSON IR (it is not
  // yet generated for experimental_maybe_from_type_alias)
  std::optional<uint32_t> subtype_resolved = std::nullopt;
  // This has no users, probably because it's missing in the JSON IR (it is not
  // yet generated for experimental_maybe_from_type_alias).
  const HandleRights* rights_resolved = nullptr;
  // This has no users, probably because it's missing in the JSON IR (it is not
  // yet generated for experimental_maybe_from_type_alias).
  const Protocol* protocol_decl = nullptr;
  // This has no users, probably because it's missing in the JSON IR (it is not
  // yet generated for experimental_maybe_from_type_alias).
  const Type* boxed_type_resolved = nullptr;

  // raw form of this type constructor's arguments
  const TypeConstructor* element_type_raw = {};
  const TypeConstructor* boxed_type_raw = {};
  const Constant* size_raw = nullptr;
  // This has no users, probably because it's missing in the JSON IR (it is not
  // yet generated for partial_type_ctors).
  const Constant* subtype_raw = nullptr;
  const Constant* rights_raw = nullptr;
  const Constant* protocol_decl_raw = nullptr;

  // Nullability is represented differently because there's only one degree of
  // freedom: if it was specified, this value is equal to kNullable
  types::Nullability nullability = types::Nullability::kNonnullable;
};

struct LayoutParameterList;
struct TypeConstraints;

// Unlike raw::TypeConstructor which will either store a name referencing a
// layout or an anonymous layout directly, in the flat AST all type constructors
// store a Reference. In the case where the type constructor represents an
// anonymous layout, the data of the anonymous layout is consumed and stored in
// the library and the corresponding type constructor contains a Reference
// whose name has AnonymousNameContext and a span covering the anonymous layout.
//
// This allows all type compilation to share the code paths through the consume
// step (i.e. RegisterDecl) and the compilation step (i.e. Typespace::Create),
// while ensuring that users cannot refer to anonymous layouts by name.
struct TypeConstructor final : public HasClone<TypeConstructor> {
  explicit TypeConstructor(Reference layout, std::unique_ptr<LayoutParameterList> parameters,
                           std::unique_ptr<TypeConstraints> constraints)
      : layout(std::move(layout)),
        parameters(std::move(parameters)),
        constraints(std::move(constraints)) {}

  std::unique_ptr<TypeConstructor> Clone() const override;

  // Set during construction.
  Reference layout;
  std::unique_ptr<LayoutParameterList> parameters;
  std::unique_ptr<TypeConstraints> constraints;

  // Set during compilation.
  const Type* type = nullptr;
  LayoutInvocation resolved_params;
};

struct LayoutParameter : public HasClone<LayoutParameter> {
 public:
  virtual ~LayoutParameter() = default;
  enum Kind {
    kIdentifier,
    kLiteral,
    kType,
  };

  explicit LayoutParameter(Kind kind, SourceSpan span) : kind(kind), span(span) {}

  // A layout parameter is either a type constructor or a constant. One of these
  // two methods must return non-null, and the other one must return null.
  virtual TypeConstructor* AsTypeCtor() const = 0;
  virtual Constant* AsConstant() const = 0;

  const Kind kind;
  SourceSpan span;
};

struct LiteralLayoutParameter final : public LayoutParameter {
  explicit LiteralLayoutParameter(std::unique_ptr<LiteralConstant> literal, SourceSpan span)
      : LayoutParameter(Kind::kLiteral, span), literal(std::move(literal)) {}

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;
  std::unique_ptr<LayoutParameter> Clone() const override;

  std::unique_ptr<LiteralConstant> literal;
};

struct TypeLayoutParameter final : public LayoutParameter {
  explicit TypeLayoutParameter(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan span)
      : LayoutParameter(Kind::kType, span), type_ctor(std::move(type_ctor)) {}

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;
  std::unique_ptr<LayoutParameter> Clone() const override;

  std::unique_ptr<TypeConstructor> type_ctor;
};

struct IdentifierLayoutParameter final : public LayoutParameter {
  explicit IdentifierLayoutParameter(Reference reference, SourceSpan span)
      : LayoutParameter(Kind::kIdentifier, span), reference(std::move(reference)) {}

  // Disambiguates between type constructor and constant. Must call after
  // resolving the reference, but before calling AsTypeCtor or AsConstant.
  void Disambiguate();

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;
  std::unique_ptr<LayoutParameter> Clone() const override;

  Reference reference;

  std::unique_ptr<TypeConstructor> as_type_ctor;
  std::unique_ptr<Constant> as_constant;
};

struct LayoutParameterList final : public HasClone<LayoutParameterList> {
  LayoutParameterList() = default;
  explicit LayoutParameterList(std::vector<std::unique_ptr<LayoutParameter>> items,
                               std::optional<SourceSpan> span)
      : items(std::move(items)), span(span) {}

  std::unique_ptr<LayoutParameterList> Clone() const override;

  std::vector<std::unique_ptr<LayoutParameter>> items;
  const std::optional<SourceSpan> span;
};

struct TypeConstraints final : public HasClone<TypeConstraints> {
  TypeConstraints() = default;
  explicit TypeConstraints(std::vector<std::unique_ptr<Constant>> items,
                           std::optional<SourceSpan> span)
      : items(std::move(items)), span(span) {}

  std::unique_ptr<TypeConstraints> Clone() const override;

  std::vector<std::unique_ptr<Constant>> items;
  const std::optional<SourceSpan> span;
};

// Const represents the _declaration_ of a constant. (For the _use_, see
// Constant. For the _value_, see ConstantValue.) A Const consists of a
// left-hand-side Name (found in Decl) and a right-hand-side Constant.
struct Const final : public Decl {
  Const(std::unique_ptr<AttributeList> attributes, Name name,
        std::unique_ptr<TypeConstructor> type_ctor, std::unique_ptr<Constant> value)
      : Decl(Kind::kConst, std::move(attributes), std::move(name)),
        type_ctor(std::move(type_ctor)),
        value(std::move(value)) {}

  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Constant> value;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Enum final : public TypeDecl {
  struct Member : public Element, public HasCopy<Member> {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<AttributeList> attributes)
        : Element(Element::Kind::kEnumMember, std::move(attributes)),
          name(name),
          value(std::move(value)) {}
    Member Copy() const override;

    SourceSpan name;
    std::unique_ptr<Constant> value;
  };

  Enum(std::unique_ptr<AttributeList> attributes, Name name,
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
  // Set only for flexible enums, and either is set depending on signedness of
  // underlying enum type.
  std::optional<int64_t> unknown_value_signed;
  std::optional<uint64_t> unknown_value_unsigned;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Bits final : public TypeDecl {
  struct Member : public Element, public HasCopy<Member> {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<AttributeList> attributes)
        : Element(Element::Kind::kBitsMember, std::move(attributes)),
          name(name),
          value(std::move(value)) {}
    Member Copy() const override;

    SourceSpan name;
    std::unique_ptr<Constant> value;
  };

  Bits(std::unique_ptr<AttributeList> attributes, Name name,
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

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Service final : public TypeDecl {
  struct Member : public Element, public HasCopy<Member> {
    Member(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
           std::unique_ptr<AttributeList> attributes)
        : Element(Element::Kind::kServiceMember, std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          name(name) {}
    Member Copy() const override;

    std::unique_ptr<TypeConstructor> type_ctor;
    SourceSpan name;
  };

  Service(std::unique_ptr<AttributeList> attributes, Name name, std::vector<Member> members)
      : TypeDecl(Kind::kService, std::move(attributes), std::move(name)),
        members(std::move(members)) {}

  std::any AcceptAny(VisitorAny* visitor) const override;

  std::vector<Member> members;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Struct;

// Historically, StructMember was a nested class inside Struct named Struct::Member. However, this
// was made a top-level class since it's not possible to forward-declare nested classes in C++. For
// backward-compatibility, Struct::Member is now an alias for this top-level StructMember.
// TODO(fxbug.dev/37535): Move this to a nested class inside Struct.
struct StructMember : public Element, public Object, public HasCopy<StructMember> {
  StructMember(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kStructMember, std::move(attributes)),
        type_ctor(std::move(type_ctor)),
        name(name),
        maybe_default_value(std::move(maybe_default_value)) {}
  StructMember Copy() const override;
  std::any AcceptAny(VisitorAny* visitor) const override;
  FieldShape fieldshape(WireFormat wire_format) const;

  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<Constant> maybe_default_value;
  const Struct* parent = nullptr;
};

struct Struct final : public TypeDecl {
  using Member = StructMember;

  Struct(std::unique_ptr<AttributeList> attributes, Name name,
         std::vector<Member> unparented_members, std::optional<types::Resourceness> resourceness)
      : TypeDecl(Kind::kStruct, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)),
        resourceness(resourceness) {
    for (auto& member : members) {
      member.parent = this;
    }
  }

  std::vector<Member> members;

  // For user-defined structs, this is set during construction. For synthesized
  // structs (requests/responses, error result success payload) it is set during
  // compilation based on the struct's members.
  std::optional<types::Resourceness> resourceness;
  std::any AcceptAny(VisitorAny* visitor) const override;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Table;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Table::Member.
struct TableMemberUsed : public Object, public HasClone<TableMemberUsed> {
  TableMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name)
      : type_ctor(std::move(type_ctor)), name(std::move(name)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  std::unique_ptr<TableMemberUsed> Clone() const override {
    return std::make_unique<TableMemberUsed>(type_ctor->Clone(), name);
  }
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Table.
struct TableMember : public Element, public Object, public HasCopy<TableMember> {
  using Used = TableMemberUsed;

  TableMember(const raw::Ordinal64* ordinal, std::unique_ptr<TypeConstructor> type, SourceSpan name,
              std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kTableMember, std::move(attributes)),
        ordinal(ordinal),
        maybe_used(std::make_unique<Used>(std::move(type), name)) {}
  TableMember(const raw::Ordinal64* ordinal, SourceSpan span,
              std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kTableMember, std::move(attributes)), ordinal(ordinal), span(span) {}
  TableMember Copy() const override;
  std::any AcceptAny(VisitorAny* visitor) const override;

  // Owned by Library::raw_ordinals.
  const raw::Ordinal64* ordinal;
  // The span for reserved table members.
  std::optional<SourceSpan> span;
  std::unique_ptr<Used> maybe_used;

 private:
  TableMember(const raw::Ordinal64* ordinal, std::optional<SourceSpan> span,
              std::unique_ptr<Used> maybe_used, std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kTableMember, std::move(attributes)),
        ordinal(ordinal),
        span(span),
        maybe_used(std::move(maybe_used)) {}
};

struct Table final : public TypeDecl {
  using Member = TableMember;

  Table(std::unique_ptr<AttributeList> attributes, Name name, std::vector<Member> members,
        types::Strictness strictness, types::Resourceness resourceness)
      : TypeDecl(Kind::kTable, std::move(attributes), std::move(name)),
        members(std::move(members)),
        strictness(strictness),
        resourceness(resourceness) {}

  std::vector<Member> members;
  const types::Strictness strictness;
  const types::Resourceness resourceness;

  std::any AcceptAny(VisitorAny* visitor) const override;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Union;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Union.
struct UnionMemberUsed : public Object, public HasClone<UnionMemberUsed> {
  UnionMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name)
      : type_ctor(std::move(type_ctor)), name(name) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  std::unique_ptr<UnionMemberUsed> Clone() const override {
    return std::make_unique<UnionMemberUsed>(type_ctor->Clone(), name);
  }

  const Union* parent = nullptr;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Union.
struct UnionMember : public Element, public Object, public HasCopy<UnionMember> {
  using Used = UnionMemberUsed;

  UnionMember(const raw::Ordinal64* ordinal, std::unique_ptr<TypeConstructor> type_ctor,
              SourceSpan name, std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kUnionMember, std::move(attributes)),
        ordinal(ordinal),
        maybe_used(std::make_unique<Used>(std::move(type_ctor), name)) {}
  UnionMember(const raw::Ordinal64* ordinal, SourceSpan span,
              std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kUnionMember, std::move(attributes)), ordinal(ordinal), span(span) {}
  UnionMember Copy() const override;
  std::any AcceptAny(VisitorAny* visitor) const override;

  // Owned by Library::raw_ordinals.
  const raw::Ordinal64* ordinal;
  // The span for reserved members.
  std::optional<SourceSpan> span;
  std::unique_ptr<Used> maybe_used;

 private:
  UnionMember(const raw::Ordinal64* ordinal, std::optional<SourceSpan> span,
              std::unique_ptr<Used> maybe_used, std::unique_ptr<AttributeList> attributes)
      : Element(Element::Kind::kUnionMember, std::move(attributes)),
        ordinal(ordinal),
        span(span),
        maybe_used(std::move(maybe_used)) {}
};

struct Union final : public TypeDecl {
  using Member = UnionMember;

  Union(std::unique_ptr<AttributeList> attributes, Name name,
        std::vector<Member> unparented_members, types::Strictness strictness,
        std::optional<types::Resourceness> resourceness)
      : TypeDecl(Kind::kUnion, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)),
        strictness(strictness),
        resourceness(resourceness) {
    for (auto& member : members) {
      if (member.maybe_used) {
        member.maybe_used->parent = this;
      }
    }
  }

  std::vector<Member> members;
  const types::Strictness strictness;

  // For user-defined unions, this is set on construction. For synthesized
  // unions (in error result responses) it is set during compilation based on
  // the unions's members.
  std::optional<types::Resourceness> resourceness;

  std::vector<std::reference_wrapper<const Member>> MembersSortedByXUnionOrdinal() const;

  std::any AcceptAny(VisitorAny* visitor) const override;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Protocol final : public TypeDecl {
  struct Method : public Element, public HasCopy<Method> {
    Method(std::unique_ptr<AttributeList> attributes, types::Strictness strictness,
           const raw::Identifier* identifier, SourceSpan name, bool has_request,
           std::unique_ptr<TypeConstructor> maybe_request, bool has_response,
           std::unique_ptr<TypeConstructor> maybe_response, bool has_error)
        : Element(Element::Kind::kProtocolMethod, std::move(attributes)),
          strictness(strictness),
          identifier(identifier),
          name(name),
          has_request(has_request),
          maybe_request(std::move(maybe_request)),
          has_response(has_response),
          maybe_response(std::move(maybe_response)),
          has_error(has_error),
          generated_ordinal64(nullptr) {
      ZX_ASSERT(this->has_request || this->has_response);
    }
    Method Copy() const override;

    types::Strictness strictness;
    // Owned by Library::raw_identifiers.
    const raw::Identifier* identifier;
    SourceSpan name;
    bool has_request;
    std::unique_ptr<TypeConstructor> maybe_request;
    bool has_response;
    std::unique_ptr<TypeConstructor> maybe_response;
    bool has_error;

    // Returns true if this method should use a result union. Result union is used if the method
    // uses error syntax or if it is a flexible two-way method.
    bool HasResultUnion() const {
      return has_error ||
             (has_request && has_response && strictness == types::Strictness::kFlexible);
    }

    // This is set to the |Protocol| instance that owns this |Method|,
    // when the |Protocol| is constructed.
    Protocol* owning_protocol = nullptr;

    // Set during compilation
    std::unique_ptr<raw::Ordinal64> generated_ordinal64;
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

  struct ComposedProtocol : public Element {
    ComposedProtocol(std::unique_ptr<AttributeList> attributes, Reference reference)
        : Element(Element::Kind::kProtocolCompose, std::move(attributes)),
          reference(std::move(reference)) {}
    ComposedProtocol Copy() const;

    Reference reference;
  };

  Protocol(std::unique_ptr<AttributeList> attributes, types::Openness openness, Name name,
           std::vector<ComposedProtocol> composed_protocols, std::vector<Method> methods)
      : TypeDecl(Kind::kProtocol, std::move(attributes), std::move(name)),
        openness(openness),
        composed_protocols(std::move(composed_protocols)),
        methods(std::move(methods)) {
    for (auto& method : this->methods) {
      method.owning_protocol = this;
    }
  }

  types::Openness openness;
  std::vector<ComposedProtocol> composed_protocols;
  std::vector<Method> methods;

  // Set during compilation.
  std::vector<MethodWithInfo> all_methods;

  std::any AcceptAny(VisitorAny* visitor) const override;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct Resource final : public Decl {
  struct Property : public Element {
    Property(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
             std::unique_ptr<AttributeList> attributes)
        : Element(Element::Kind::kResourceProperty, std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          name(name) {}
    Property Copy() const;

    std::unique_ptr<TypeConstructor> type_ctor;
    SourceSpan name;
  };

  Resource(std::unique_ptr<AttributeList> attributes, Name name,
           std::unique_ptr<TypeConstructor> subtype_ctor, std::vector<Property> properties)
      : Decl(Kind::kResource, std::move(attributes), std::move(name)),
        subtype_ctor(std::move(subtype_ctor)),
        properties(std::move(properties)) {}

  // Set during construction.
  std::unique_ptr<TypeConstructor> subtype_ctor;
  std::vector<Property> properties;

  Property* LookupProperty(std::string_view name);

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct TypeAlias final : public Decl {
  TypeAlias(std::unique_ptr<AttributeList> attributes, Name name,
            std::unique_ptr<TypeConstructor> partial_type_ctor)
      : Decl(Kind::kTypeAlias, std::move(attributes), std::move(name)),
        partial_type_ctor(std::move(partial_type_ctor)) {}

  // The shape of this type constructor is more constrained than just being a
  // "partial" type constructor - it is either a normal type constructor
  // referring directly to a non-type-alias with all layout parameters fully
  // specified (e.g. alias foo = array<T, 3>), or it is a type constructor
  // referring to another type alias that has no layout parameters (e.g. alias
  // bar = foo).
  // The constraints on the other hand are indeed "partial" - any type alias
  // at any point in a "type alias chain" can specify a constraint, but any
  // constraint can only specified once. This behavior will change in
  // fxbug.dev/74193.
  std::unique_ptr<TypeConstructor> partial_type_ctor;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

struct NewType final : public TypeDecl {
  NewType(std::unique_ptr<AttributeList> attributes, Name name,
          std::unique_ptr<TypeConstructor> type_ctor)
      : TypeDecl(Kind::kNewType, std::move(attributes), std::move(name)),
        type_ctor(std::move(type_ctor)) {}

  // Note that unlike in TypeAlias, we are not calling this partial type constructor. Whether or
  // not all the constraints for this type are applied is irrelevant to us down the line - all we
  // care is that we have a type constructor to define a type.
  std::unique_ptr<TypeConstructor> type_ctor;

  std::any AcceptAny(VisitorAny* visitor) const override;

 private:
  std::unique_ptr<Decl> SplitImpl(VersionRange range) const override;
};

// This class is used to manage a library's set of direct dependencies, i.e.
// those imported with "using" statements.
class Dependencies {
 public:
  enum class RegisterResult {
    kSuccess,
    kDuplicate,
    kCollision,
  };

  // Registers a dependency to a library. The registration name is |maybe_alias|
  // if provided, otherwise the library's name. Afterwards, Dependencies::Lookup
  // will return |dep_library| given the registration name.
  RegisterResult Register(const SourceSpan& span, std::string_view filename, Library* dep_library,
                          const std::unique_ptr<raw::Identifier>& maybe_alias);

  // Returns true if this dependency set contains a library with the given name and filename.
  bool Contains(std::string_view filename, const std::vector<std::string_view>& name);

  // Looks up a dependency by filename (within the importing library, since
  // "using" statements are file-scoped) and name (of the imported library).
  // Also marks the library as used. Returns null if no library is found.
  Library* LookupAndMarkUsed(std::string_view filename,
                             const std::vector<std::string_view>& name) const;

  // VerifyAllDependenciesWereUsed reports an error for each dependency imported
  // with `using` that was never used in the file.
  void VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter);

  // Returns all the dependencies.
  const std::set<Library*>& all() const { return dependencies_aggregate_; }

  std::vector<std::pair<Library*, SourceSpan>> library_references() {
    std::vector<std::pair<Library*, SourceSpan>> references;
    references.reserve(refs_.size());
    for (auto& ref : refs_) {
      auto library_ref = std::make_pair(ref->library, ref->span);
      references.emplace_back(library_ref);
    }
    return references;
  }

 private:
  // A reference to a library, derived from a "using" statement.
  struct LibraryRef {
    LibraryRef(SourceSpan span, Library* library) : span(span), library(library) {}

    const SourceSpan span;
    Library* const library;
    bool used = false;
  };

  // Per-file information about imports.
  struct PerFile {
    // References to dependencies, keyed by library name or by alias.
    std::map<std::vector<std::string_view>, LibraryRef*> refs;
    // Set containing ref->library for every ref in |refs|.
    std::set<Library*> libraries;
  };

  std::vector<std::unique_ptr<LibraryRef>> refs_;
  // The string keys are owned by SourceFile objects.
  std::map<std::string_view, std::unique_ptr<PerFile>> by_filename_;
  std::set<Library*> dependencies_aggregate_;
};

struct LibraryComparator;

struct Library final : public Element {
  Library() : Element(Element::Kind::kLibrary, std::make_unique<AttributeList>()) {}

  // Creates the root library which holds all Builtin decls.
  static std::unique_ptr<Library> CreateRootLibrary();

  // Runs a function on every element in the library via depth-first traversal.
  // Runs it on the library itself, on all Decls, and on all their members.
  void TraverseElements(const fit::function<void(Element*)>& fn);

  struct Declarations {
    // Inserts a declaration. When inserting builtins, this must be called in
    // order of Builtin::Identity. For other decls, the order doesn't matter.
    Decl* Insert(std::unique_ptr<Decl> decl);
    // Looks up a builtin. Must have inserted it already with InsertBuiltin.
    Builtin* LookupBuiltin(Builtin::Identity id) const;

    // Contains all the declarations owned by the vectors below.
    std::multimap<std::string_view, Decl*> all;

    std::vector<std::unique_ptr<Bits>> bits;
    std::vector<std::unique_ptr<Builtin>> builtins;
    std::vector<std::unique_ptr<Const>> consts;
    std::vector<std::unique_ptr<Enum>> enums;
    std::vector<std::unique_ptr<NewType>> new_types;
    std::vector<std::unique_ptr<Protocol>> protocols;
    std::vector<std::unique_ptr<Resource>> resources;
    std::vector<std::unique_ptr<Service>> services;
    std::vector<std::unique_ptr<Struct>> structs;
    std::vector<std::unique_ptr<Table>> tables;
    std::vector<std::unique_ptr<TypeAlias>> type_aliases;
    std::vector<std::unique_ptr<Union>> unions;
  };

  std::vector<std::string_view> name;
  // There is no unique SourceSpan for a library's name since it can be declared
  // in multiple files, but we store an arbitrary one to use in error messages.
  SourceSpan arbitrary_name_span;
  // stores all library name declaration location
  std::vector<SourceSpan> library_name_declarations;
  // Set during AvailabilityStep.
  std::optional<Platform> platform;
  Dependencies dependencies;
  // Populated by ConsumeStep, and then rewritten by ResolveStep.
  Declarations declarations;
  // Contains the same decls as `declarations`, but in a topologically sorted
  // order, i.e. later decls only depend on earlier ones. Populated by SortStep.
  std::vector<const Decl*> declaration_order;
  // Raw AST objects pointed to by certain flat AST nodes. We store them on the
  // Library because there is no unique ownership (e.g. multiple Table::Member
  // instances can point to the same raw::Ordinal64 after decomposition).
  std::vector<std::unique_ptr<raw::Literal>> raw_literals;
  std::vector<std::unique_ptr<raw::Identifier>> raw_identifiers;
  std::vector<std::unique_ptr<raw::Ordinal64>> raw_ordinals;
};

struct LibraryComparator {
  bool operator()(const flat::Library* lhs, const flat::Library* rhs) const {
    ZX_ASSERT(!lhs->name.empty());
    ZX_ASSERT(!rhs->name.empty());
    return lhs->name < rhs->name;
  }
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_
