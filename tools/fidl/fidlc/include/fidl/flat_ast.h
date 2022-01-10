// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#compilation
// for documentation

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_

#include <lib/fit/function.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "experimental_flags.h"
#include "flat/attributes.h"
#include "flat/name.h"
#include "flat/object.h"
#include "flat/types.h"
#include "flat/typespace.h"
#include "flat/values.h"
#include "reporter.h"
#include "type_shape.h"
#include "types.h"
#include "virtual_source_file.h"

namespace fidl::raw {

class File;
class Identifier;
class Ordinal64;
class SourceElement;

}  // namespace fidl::raw

namespace fidl::flat {

using reporter::Reporter;
using reporter::ReporterMixin;
using utils::identity_t;

template <typename T>
struct PtrCompare {
  bool operator()(const T* left, const T* right) const { return *left < *right; }
};

class Typespace;
struct Decl;
class Library;
class CompileStep;

bool HasSimpleLayout(const Decl* decl);

// This is needed (for now) to work around declaration order issues.
std::string LibraryName(const Library* library, std::string_view separator);

struct Decl : public Attributable {
  virtual ~Decl() {}

  enum struct Kind {
    kBits,
    kConst,
    kEnum,
    kProtocol,
    kResource,
    kService,
    kStruct,
    kTable,
    kUnion,
    kTypeAlias,
  };

  static AttributePlacement AttributePlacement(Kind kind) {
    switch (kind) {
      case Kind::kBits:
        return AttributePlacement::kBitsDecl;
      case Kind::kConst:
        return AttributePlacement::kConstDecl;
      case Kind::kEnum:
        return AttributePlacement::kEnumDecl;
      case Kind::kProtocol:
        return AttributePlacement::kProtocolDecl;
      case Kind::kResource:
        return AttributePlacement::kResourceDecl;
      case Kind::kService:
        return AttributePlacement::kServiceDecl;
      case Kind::kStruct:
        return AttributePlacement::kStructDecl;
      case Kind::kTable:
        return AttributePlacement::kTableDecl;
      case Kind::kUnion:
        return AttributePlacement::kUnionDecl;
      case Kind::kTypeAlias:
        return AttributePlacement::kTypeAliasDecl;
    }
  }

  Decl(Kind kind, std::unique_ptr<AttributeList> attributes, Name name)
      : Attributable(AttributePlacement(kind), std::move(attributes)),
        kind(kind),
        name(std::move(name)) {}

  const Kind kind;
  const Name name;

  std::string GetName() const;

  bool compiling = false;
  bool compiled = false;
};

struct TypeDecl : public Decl, public Object {
  TypeDecl(Kind kind, std::unique_ptr<AttributeList> attributes, Name name)
      : Decl(kind, std::move(attributes), std::move(name)) {}

  bool recursive = false;
};

struct TypeConstructor;
struct TypeAlias;
class Protocol;

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

// Unlike raw::TypeConstructor which will either store a name referencing
// a layout or an anonymous layout directly, in the flat AST all type
// constructors store a Name. In the case where the type constructor represents
// an anonymous layout, the data of the anonymous layout is consumed and stored
// in the Typespace and the corresponding type constructor contains a Name with
// is_anonymous=true and with a span covering the anonymous layout.
//
// This allows all type compilation to share the code paths through the
// consume step (i.e. RegisterDecl) and the compilation step (i.e. Typespace::Create),
// while ensuring that users cannot refer to anonymous layouts by name.
struct TypeConstructor final {
  TypeConstructor(Name name, std::unique_ptr<LayoutParameterList> parameters,
                  std::unique_ptr<TypeConstraints> constraints)
      : name(std::move(name)),
        parameters(std::move(parameters)),
        constraints(std::move(constraints)) {}

  // Returns a type constructor for the size type (used for bounds).
  static std::unique_ptr<TypeConstructor> CreateSizeType();

  // Set during construction.
  const Name name;
  std::unique_ptr<LayoutParameterList> parameters;
  std::unique_ptr<TypeConstraints> constraints;

  // Set during compilation.
  const Type* type = nullptr;
  LayoutInvocation resolved_params;
};

struct LayoutParameter {
 public:
  virtual ~LayoutParameter() = default;
  enum Kind {
    kIdentifier,
    kLiteral,
    kType,
  };

  explicit LayoutParameter(Kind kind, SourceSpan span) : kind(kind), span(span) {}

  // TODO(fxbug.dev/75112): Providing these virtual methods rather than handling
  // each case individually in the caller makes it harder to provide more precise
  // error messages. For example, using this pattern we'd only know that a parameter
  // failed to be interpreted as a type and not the specifics about why it failed
  // (was this actually a string literal? did it look like a type but fail to
  // resolve? did it look like a type but actually point to a constant?).
  // Addressing the bug might involve refactoring this part of the code to move
  // more logic into the caller. This might be acceptable when the caller is
  // type compilation (it probably needs to know these details anyways), but
  // less so when it's a consumer of compiled results that needs to reconstruct
  // details about the type constructor (e.g. during declaration sorting or
  // JSON generation).

  // TODO(fxbug.dev/75805): The return types should be optional references

  // Returns the interpretation of this layout parameter as a type if possible
  // or nullptr otherwise. There are no guarantees that the returned type has
  // been compiled or will actually successfully compile.
  virtual TypeConstructor* AsTypeCtor() const = 0;

  // Returns the interpretation of this layout parameter as a constant if possible
  // or nullptr otherwise. There are no guarantees that the returned constant has
  // been compiled or will actually successfully compile.
  virtual Constant* AsConstant() const = 0;

  const Kind kind;
  SourceSpan span;
};

struct LiteralLayoutParameter final : public LayoutParameter {
  explicit LiteralLayoutParameter(std::unique_ptr<LiteralConstant> literal, SourceSpan span)
      : LayoutParameter(Kind::kLiteral, span), literal(std::move(literal)) {}

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;
  std::unique_ptr<LiteralConstant> literal;
};

struct TypeLayoutParameter final : public LayoutParameter {
  explicit TypeLayoutParameter(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan span)
      : LayoutParameter(Kind::kType, span), type_ctor(std::move(type_ctor)) {}

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;
  std::unique_ptr<TypeConstructor> type_ctor;
};

struct IdentifierLayoutParameter final : public LayoutParameter {
  explicit IdentifierLayoutParameter(Name name, SourceSpan span)
      : LayoutParameter(Kind::kIdentifier, span), name(std::move(name)) {}

  TypeConstructor* AsTypeCtor() const override;
  Constant* AsConstant() const override;

  // Stores an interpretation of this layout as a TypeConstructor, if asked
  // at some point (i.e. on demand by calling AsTypeCtor). We store this to
  // store a reference to the compiled Type and LayoutInvocation
  mutable std::unique_ptr<TypeConstructor> as_type_ctor;

  // Stores an interpretation of this layout as a Constant, if asked at some
  // point (i.e. on demand by calling AsConstant). We store this to store a
  // reference to the compiled ConstantValue
  mutable std::unique_ptr<Constant> as_constant;
  const Name name;
};

struct LayoutParameterList {
  explicit LayoutParameterList(std::vector<std::unique_ptr<LayoutParameter>> items,
                               std::optional<SourceSpan> span)
      : items(std::move(items)), span(span) {}

  std::vector<std::unique_ptr<LayoutParameter>> items;

  // Span of all parameters, or of the type constructor's name if there are none
  // (used for "too few parameters" errors when 0 are given). For example:
  //
  //     string    vector<bool>    array<uint32, 3>
  //     ^~~~~~          ^~~~~~         ^~~~~~~~~~~
  //
  // It is null for generated types where no span is available. In these cases
  // we should never attempt to access it for an error message.
  const std::optional<SourceSpan> span;
};

struct TypeConstraints {
  explicit TypeConstraints(std::vector<std::unique_ptr<Constant>> items,
                           std::optional<SourceSpan> span)
      : items(std::move(items)), span(span) {}

  std::vector<std::unique_ptr<Constant>> items;
  // Similar to LayoutParameterList's span but for constraints.
  const std::optional<SourceSpan> span;
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
  Const(std::unique_ptr<AttributeList> attributes, Name name,
        std::unique_ptr<TypeConstructor> type_ctor, std::unique_ptr<Constant> value)
      : Decl(Kind::kConst, std::move(attributes), std::move(name)),
        type_ctor(std::move(type_ctor)),
        value(std::move(value)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Constant> value;
};

struct Enum final : public TypeDecl {
  struct Member : public Attributable {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<AttributeList> attributes)
        : Attributable(AttributePlacement::kEnumMember, std::move(attributes)),
          name(name),
          value(std::move(value)) {}
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
};

struct Bits final : public TypeDecl {
  struct Member : public Attributable {
    Member(SourceSpan name, std::unique_ptr<Constant> value,
           std::unique_ptr<AttributeList> attributes)
        : Attributable(AttributePlacement::kBitsMember, std::move(attributes)),
          name(name),
          value(std::move(value)) {}
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
};

struct Service final : public TypeDecl {
  struct Member : public Attributable {
    Member(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
           std::unique_ptr<AttributeList> attributes)
        : Attributable(AttributePlacement::kServiceMember, std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          name(name) {}

    std::unique_ptr<TypeConstructor> type_ctor;
    SourceSpan name;
  };

  Service(std::unique_ptr<AttributeList> attributes, Name name, std::vector<Member> members)
      : TypeDecl(Kind::kService, std::move(attributes), std::move(name)),
        members(std::move(members)) {}

  std::any AcceptAny(VisitorAny* visitor) const override;

  std::vector<Member> members;
};

struct Struct;

// Historically, StructMember was a nested class inside Struct named Struct::Member. However, this
// was made a top-level class since it's not possible to forward-declare nested classes in C++. For
// backward-compatibility, Struct::Member is now an alias for this top-level StructMember.
// TODO(fxbug.dev/37535): Move this to a nested class inside Struct.
struct StructMember : public Attributable, public Object {
  StructMember(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
               std::unique_ptr<Constant> maybe_default_value,
               std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kStructMember, std::move(attributes)),
        type_ctor(std::move(type_ctor)),
        name(std::move(name)),
        maybe_default_value(std::move(maybe_default_value)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<Constant> maybe_default_value;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  const Struct* parent = nullptr;
};

struct Struct final : public TypeDecl {
  using Member = StructMember;

  Struct(std::unique_ptr<AttributeList> attributes, Name name,
         std::vector<Member> unparented_members, std::optional<types::Resourceness> resourceness,
         bool is_request_or_response = false)
      : TypeDecl(Kind::kStruct, std::move(attributes), std::move(name)),
        members(std::move(unparented_members)),
        resourceness(resourceness),
        is_request_or_response(is_request_or_response) {
    for (auto& member : members) {
      member.parent = this;
    }
  }

  std::vector<Member> members;

  // For user-defined structs, this is set during construction. For synthesized
  // structs (requests/responses, error result success payload) it is set during
  // compilation based on the struct's members.
  std::optional<types::Resourceness> resourceness;

  // This is true iff this struct is a method request/response in a transaction header.
  const bool is_request_or_response;
  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Table;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Table::Member.
struct TableMemberUsed : public Object {
  TableMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
                  std::unique_ptr<Constant> maybe_default_value)
      : type_ctor(std::move(type_ctor)),
        name(std::move(name)),
        maybe_default_value(std::move(maybe_default_value)) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;
  std::unique_ptr<Constant> maybe_default_value;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Table.
struct TableMember : public Attributable, public Object {
  using Used = TableMemberUsed;

  TableMember(std::unique_ptr<raw::Ordinal64> ordinal, std::unique_ptr<TypeConstructor> type,
              SourceSpan name, std::unique_ptr<Constant> maybe_default_value,
              std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kTableMember, std::move(attributes)),
        ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type), name, std::move(maybe_default_value))) {}
  TableMember(std::unique_ptr<raw::Ordinal64> ordinal, std::unique_ptr<TypeConstructor> type,
              SourceSpan name, std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kTableMember, std::move(attributes)),
        ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type), name, nullptr)) {}
  TableMember(std::unique_ptr<raw::Ordinal64> ordinal, SourceSpan span,
              std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kTableMember, std::move(attributes)),
        ordinal(std::move(ordinal)),
        span(span) {}

  std::unique_ptr<raw::Ordinal64> ordinal;

  // The span for reserved table members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
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
};

struct Union;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Union.
struct UnionMemberUsed : public Object {
  UnionMemberUsed(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
                  std::unique_ptr<AttributeList> attributes)
      : type_ctor(std::move(type_ctor)), name(name) {}
  std::unique_ptr<TypeConstructor> type_ctor;
  SourceSpan name;

  std::any AcceptAny(VisitorAny* visitor) const override;

  FieldShape fieldshape(WireFormat wire_format) const;

  const Union* parent = nullptr;
};

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Union.
struct UnionMember : public Attributable, public Object {
  using Used = UnionMemberUsed;

  UnionMember(std::unique_ptr<raw::Ordinal64> ordinal, std::unique_ptr<TypeConstructor> type_ctor,
              SourceSpan name, std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kUnionMember, std::move(attributes)),
        ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type_ctor), name, std::move(attributes))) {}
  UnionMember(std::unique_ptr<raw::Ordinal64> ordinal, SourceSpan span,
              std::unique_ptr<AttributeList> attributes)
      : Attributable(AttributePlacement::kUnionMember, std::move(attributes)),
        ordinal(std::move(ordinal)),
        span(span) {}

  std::unique_ptr<raw::Ordinal64> ordinal;

  // The span for reserved members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
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
};

class Protocol final : public TypeDecl {
 public:
  struct Method : public Attributable {
    Method(Method&&) = default;
    Method& operator=(Method&&) = default;

    Method(std::unique_ptr<AttributeList> attributes, std::unique_ptr<raw::Identifier> identifier,
           SourceSpan name, bool has_request, std::unique_ptr<TypeConstructor> maybe_request,
           bool has_response, std::unique_ptr<TypeConstructor> maybe_response, bool has_error)
        : Attributable(AttributePlacement::kMethod, std::move(attributes)),
          identifier(std::move(identifier)),
          name(name),
          has_request(has_request),
          maybe_request(std::move(maybe_request)),
          has_response(has_response),
          maybe_response(std::move(maybe_response)),
          has_error(has_error),
          generated_ordinal64(nullptr) {
      assert(this->has_request || this->has_response);
    }

    std::unique_ptr<raw::Identifier> identifier;
    SourceSpan name;
    bool has_request;
    std::unique_ptr<TypeConstructor> maybe_request;
    bool has_response;
    std::unique_ptr<TypeConstructor> maybe_response;
    bool has_error;
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

  struct ComposedProtocol : public Attributable {
    ComposedProtocol(std::unique_ptr<AttributeList> attributes, Name name)
        : Attributable(AttributePlacement::kProtocolCompose, std::move(attributes)),
          name(std::move(name)) {}

    Name name;
  };

  Protocol(std::unique_ptr<AttributeList> attributes, Name name,
           std::vector<ComposedProtocol> composed_protocols, std::vector<Method> methods)
      : TypeDecl(Kind::kProtocol, std::move(attributes), std::move(name)),
        composed_protocols(std::move(composed_protocols)),
        methods(std::move(methods)) {
    for (auto& method : this->methods) {
      method.owning_protocol = this;
    }
  }

  std::vector<ComposedProtocol> composed_protocols;
  std::vector<Method> methods;
  std::vector<MethodWithInfo> all_methods;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Resource final : public Decl {
  struct Property : public Attributable {
    Property(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
             std::unique_ptr<AttributeList> attributes)
        : Attributable(AttributePlacement::kResourceProperty, std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          name(name) {}
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
};

template <template <typename> typename Constness, typename MemberFn>
static void ForEachDeclMemberHelper(Constness<Decl>* decl, MemberFn f) {
  switch (decl->kind) {
    case Decl::Kind::kBits:
      for (auto& member : static_cast<Constness<Bits>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kConst:
      break;
    case Decl::Kind::kEnum:
      for (auto& member : static_cast<Constness<Enum>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kProtocol:
      for (auto& method_with_info : static_cast<Constness<Protocol>*>(decl)->all_methods) {
        f(method_with_info.method);
      }
      break;
    case Decl::Kind::kResource:
      for (auto& member : static_cast<Constness<Resource>*>(decl)->properties) {
        f(&member);
      }
      break;
    case Decl::Kind::kService:
      for (auto& member : static_cast<Constness<Service>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kStruct:
      for (auto& member : static_cast<Constness<Struct>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kTable:
      for (auto& member : static_cast<Constness<Table>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kUnion:
      for (auto& member : static_cast<Constness<Union>*>(decl)->members) {
        f(&member);
      }
      break;
    case Decl::Kind::kTypeAlias:
      break;
  }  // switch
}

template <typename MemberFn>
static void ForEachDeclMember(const Decl* decl, MemberFn f) {
  ForEachDeclMemberHelper<std::add_const_t>(decl, f);
}

template <typename MemberFn>
static void ForEachDeclMember(Decl* decl, MemberFn f) {
  ForEachDeclMemberHelper<std::remove_const_t>(decl, f);
}

// Wrapper class around a Library to provide specific methods to TypeTemplates.
// Unlike making a direct friend relationship, this approach:
// 1. avoids having to declare every TypeTemplate subclass as a friend
// 2. only exposes the methods that are needed from the Library to the TypeTemplate.
class LibraryMediator : private ReporterMixin {
 public:
  explicit LibraryMediator(Library* library, CompileStep* compile_step, Reporter* reporter)
      : ReporterMixin(reporter), library_(library), compile_step_(compile_step) {}

  using ReporterMixin::Fail;

  // Top level methods for resolving layout parameters. These are used by
  // TypeTemplates.
  bool ResolveParamAsType(const flat::TypeTemplate* layout,
                          const std::unique_ptr<LayoutParameter>& param,
                          const Type** out_type) const;
  bool ResolveParamAsSize(const flat::TypeTemplate* layout,
                          const std::unique_ptr<LayoutParameter>& param,
                          const Size** out_size) const;

  // Top level methods for resolving constraints. These are used by Types
  enum class ConstraintKind {
    kHandleSubtype,
    kHandleRights,
    kSize,
    kNullability,
    kProtocol,
  };

  struct ResolvedConstraint {
    ConstraintKind kind;

    union Value {
      uint32_t handle_subtype;
      const HandleRights* handle_rights;
      const Size* size;
      // Storing a value for nullability is redundant, since there's only one possible value - if we
      // resolved to optional, then the caller knows that the resulting value is
      // types::Nullability::kNullable.
      const Protocol* protocol_decl;
    } value;
  };

  // Convenience method to iterate through the possible interpretations, returning the first one
  // that succeeds. This is valid because the interpretations are mutually exclusive, since a Name
  // can only ever refer to one kind of thing.
  bool ResolveConstraintAs(const std::unique_ptr<Constant>& constraint,
                           const std::vector<ConstraintKind>& interpretations,
                           Resource* resource_decl, ResolvedConstraint* out) const;

  // These methods forward their implementation to the library_. They are used
  // by the top level methods above
  bool ResolveType(TypeConstructor* type) const;
  bool ResolveSizeBound(Constant* size_constant, const Size** out_size) const;
  bool ResolveAsOptional(Constant* constant) const;
  bool ResolveAsHandleSubtype(Resource* resource, const std::unique_ptr<Constant>& constant,
                              uint32_t* out_obj_type) const;
  bool ResolveAsHandleRights(Resource* resource, Constant* constant,
                             const HandleRights** out_rights) const;
  bool ResolveAsProtocol(const Constant* size_constant, const Protocol** out_decl) const;
  Decl* LookupDeclByName(Name::Key name) const;

  // Used specifically in TypeAliasTypeTemplates to recursively compile the next
  // type alias.
  void CompileDecl(Decl* decl) const;

  // Use in TypeAliasTypeTemplates to check for decl cycles before trying to
  // compile the next type alias and to get the cycle to use in the error
  // report.
  std::optional<std::vector<const Decl*>> GetDeclCycle(const Decl* decl) const;

 private:
  Library* library_;
  CompileStep* compile_step_;
};

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

  enum class LookupMode {
    kSilent,
    kUse,
  };

  // Looks up a dependent library by |filename| and |name|, and optionally marks
  // it as used or not.
  bool Lookup(std::string_view filename, const std::vector<std::string_view>& name, LookupMode mode,
              Library** out_library) const;

  // VerifyAllDependenciesWereUsed reports an error for each dependency imported
  // with `using` that was never used in the file.
  void VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter);

  const std::set<Library*>& dependencies() const { return dependencies_aggregate_; }

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

class Libraries {
 public:
  Libraries();

  // Insert |library|.
  bool Insert(std::unique_ptr<Library> library);

  // Lookup a library by its |library_name|.
  bool Lookup(const std::vector<std::string_view>& library_name, Library** out_library) const;

  // Registers a new attribute schema under the given name, and returns it.
  AttributeSchema& AddAttributeSchema(std::string name) {
    auto [it, inserted] = attribute_schemas_.try_emplace(std::move(name));
    assert(inserted && "do not add schemas twice");
    return it->second;
  }

  const AttributeSchema& RetrieveAttributeSchema(Reporter* reporter, const Attribute* attribute,
                                                 bool warn_on_typo = false) const;

  std::set<std::vector<std::string_view>> Unused(const Library* target_library) const;

 private:
  std::map<std::vector<std::string_view>, std::unique_ptr<Library>> all_libraries_;
  // Use transparent comparator std::less<> to allow std::string_view lookups.
  std::map<std::string, AttributeSchema, std::less<>> attribute_schemas_;
};

using MethodHasher = fit::function<raw::Ordinal64(
    const std::vector<std::string_view>& library_name, const std::string_view& protocol_name,
    const std::string_view& selector_name, const raw::SourceElement& source_element)>;

struct LibraryComparator;

class Library : private Attributable, private ReporterMixin {
  friend class AttributeSchema;
  friend class StepBase;
  friend class ConsumeStep;
  friend class SortStep;
  friend class CompileStep;
  friend class VerifyResourcenessStep;
  friend class VerifyAttributesStep;
  friend class VerifyInlineSizeStep;
  friend class VerifyDependenciesStep;

 public:
  Library(const Libraries* all_libraries, Reporter* reporter, Typespace* typespace,
          MethodHasher method_hasher, ExperimentalFlags experimental_flags)
      : Attributable(AttributePlacement::kLibrary, nullptr),
        ReporterMixin(reporter),
        all_libraries_(all_libraries),
        typespace_(typespace),
        method_hasher_(std::move(method_hasher)),
        experimental_flags_(experimental_flags) {}

  bool ConsumeFile(std::unique_ptr<raw::File> file);
  bool Compile();
  std::set<const Library*, LibraryComparator> DirectDependencies() const;

  const std::vector<std::string_view>& name() const { return library_name_; }
  const AttributeList* GetAttributes() const { return attributes.get(); }

  // Returns nullptr when the |name| cannot be resolved to a
  // Name. Otherwise it returns the declaration.
  Decl* LookupDeclByName(Name::Key name) const;

  const std::set<Library*>& dependencies() const;

  std::vector<std::string_view> library_name_;

  std::vector<std::unique_ptr<Bits>> bits_declarations_;
  std::vector<std::unique_ptr<Const>> const_declarations_;
  std::vector<std::unique_ptr<Enum>> enum_declarations_;
  std::vector<std::unique_ptr<Protocol>> protocol_declarations_;
  std::vector<std::unique_ptr<Resource>> resource_declarations_;
  std::vector<std::unique_ptr<Service>> service_declarations_;
  std::vector<std::unique_ptr<Struct>> struct_declarations_;
  std::vector<std::unique_ptr<Table>> table_declarations_;
  std::vector<std::unique_ptr<Union>> union_declarations_;
  std::vector<std::unique_ptr<TypeAlias>> type_alias_declarations_;

  // All Decl pointers here are non-null and are owned by the
  // various foo_declarations_.
  std::vector<const Decl*> declaration_order_;

 private:
  // TODO(fxbug.dev/7920): Rationalize the use of names. Here, a simple name is
  // one that is not scoped, it is just text. An anonymous name is one that
  // is guaranteed to be unique within the library, and a derived name is one
  // that is library scoped but derived from the concatenated components using
  // underscores as delimiters.
  SourceSpan GeneratedSimpleName(std::string_view name);

  Dependencies dependencies_;
  const Libraries* all_libraries_;

  // All Decl pointers here are non-null. They are owned by the various
  // foo_declarations_ members of this Library object, or of one of the Library
  // objects in dependencies_.
  std::map<Name::Key, Decl*> declarations_;

  Typespace* typespace_;
  const MethodHasher method_hasher_;
  const ExperimentalFlags experimental_flags_;

  VirtualSourceFile generated_source_file_{"generated"};
  friend class LibraryMediator;
};

struct LibraryComparator {
  bool operator()(const flat::Library* lhs, const flat::Library* rhs) const {
    assert(!lhs->name().empty());
    assert(!rhs->name().empty());
    return lhs->name() < rhs->name();
  }
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_AST_H_
