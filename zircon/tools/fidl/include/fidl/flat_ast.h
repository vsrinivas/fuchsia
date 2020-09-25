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
#include "experimental_flags.h"
#include "flat/name.h"
#include "flat/object.h"
#include "flat/types.h"
#include "flat/values.h"
#include "raw_ast.h"
#include "reporter.h"
#include "type_shape.h"
#include "types.h"
#include "virtual_source_file.h"

namespace fidl {
namespace flat {

using diagnostics::Diagnostic;
using diagnostics::ErrorDef;
using reporter::Reporter;

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

struct Decl {
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

struct TypeDecl : public Decl, public Object {
  TypeDecl(Kind kind, std::unique_ptr<raw::AttributeList> attributes, Name name)
      : Decl(kind, std::move(attributes), std::move(name)) {}

  bool recursive = false;
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
                  std::optional<Name> handle_subtype_identifier,
                  std::unique_ptr<Constant> handle_rights, std::unique_ptr<Constant> maybe_size,
                  types::Nullability nullability)
      : name(std::move(name)),
        maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
        handle_subtype(handle_subtype),
        handle_subtype_identifier(std::move(handle_subtype_identifier)),
        handle_rights(std::move(handle_rights)),
        maybe_size(std::move(maybe_size)),
        nullability(nullability) {}

  // Returns a type constructor for the size type (used for bounds).
  static std::unique_ptr<TypeConstructor> CreateSizeType();

  // Set during construction.
  const Name name;
  const std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
  const std::optional<types::HandleSubtype> handle_subtype;
  const std::optional<Name> handle_subtype_identifier;
  const std::unique_ptr<Constant> handle_rights;
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
  // Set only for flexible enums, and either is set depending on signedness of
  // underlying enum type.
  std::optional<int64_t> unknown_value_signed;
  std::optional<uint64_t> unknown_value_unsigned;
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
// TODO(fxbug.dev/37535): Move this to a nested class inside Struct.
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
         std::vector<Member> unparented_members, types::Resourceness resourceness,
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
  const types::Resourceness resourceness;

  // This is true iff this struct is a method request/response in a transaction header.
  const bool is_request_or_response;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Table;

// See the comment on the StructMember class for why this is a top-level class.
// TODO(fxbug.dev/37535): Move this to a nested class inside Table::Member.
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
// TODO(fxbug.dev/37535): Move this to a nested class inside Table.
struct TableMember : public Object {
  using Used = TableMemberUsed;

  TableMember(std::unique_ptr<raw::Ordinal64> ordinal, std::unique_ptr<TypeConstructor> type,
              SourceSpan name, std::unique_ptr<Constant> maybe_default_value,
              std::unique_ptr<raw::AttributeList> attributes)
      : ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type), std::move(name),
                                          std::move(maybe_default_value), std::move(attributes))) {}
  TableMember(std::unique_ptr<raw::Ordinal64> ordinal, SourceSpan span)
      : ordinal(std::move(ordinal)), span(span) {}

  std::unique_ptr<raw::Ordinal64> ordinal;

  // The span for reserved table members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Table final : public TypeDecl {
  using Member = TableMember;

  Table(std::unique_ptr<raw::AttributeList> attributes, Name name, std::vector<Member> members,
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
// TODO(fxbug.dev/37535): Move this to a nested class inside Union.
struct UnionMember : public Object {
  using Used = UnionMemberUsed;

  UnionMember(std::unique_ptr<raw::Ordinal64> ordinal, std::unique_ptr<TypeConstructor> type_ctor,
              SourceSpan name, std::unique_ptr<raw::AttributeList> attributes)
      : ordinal(std::move(ordinal)),
        maybe_used(std::make_unique<Used>(std::move(type_ctor), name, std::move(attributes))) {}
  UnionMember(std::unique_ptr<raw::Ordinal64> ordinal, SourceSpan span)
      : ordinal(std::move(ordinal)), span(span) {}

  std::unique_ptr<raw::Ordinal64> ordinal;

  // The span for reserved members.
  std::optional<SourceSpan> span;

  std::unique_ptr<Used> maybe_used;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Union final : public TypeDecl {
  using Member = UnionMember;

  Union(std::unique_ptr<raw::AttributeList> attributes, Name name,
        std::vector<Member> unparented_members, types::Strictness strictness,
        types::Resourceness resourceness)
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
  const types::Resourceness resourceness;

  std::vector<std::reference_wrapper<const Member>> MembersSortedByXUnionOrdinal() const;

  std::any AcceptAny(VisitorAny* visitor) const override;
};

struct Protocol final : public TypeDecl {
  struct Method {
    Method(Method&&) = default;
    Method& operator=(Method&&) = default;

    Method(std::unique_ptr<raw::AttributeList> attributes,
           std::unique_ptr<raw::Ordinal64> generated_ordinal64, SourceSpan name,
           Struct* maybe_request, Struct* maybe_response)
        : attributes(std::move(attributes)),
          generated_ordinal64(std::move(generated_ordinal64)),
          name(std::move(name)),
          maybe_request(maybe_request),
          maybe_response(maybe_response) {
      assert(this->maybe_request != nullptr || this->maybe_response != nullptr);
    }

    std::unique_ptr<raw::AttributeList> attributes;
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

struct Resource final : public Decl {
  struct Property {
    Property(std::unique_ptr<TypeConstructor> type_ctor, SourceSpan name,
             std::unique_ptr<raw::AttributeList> attributes)
        : type_ctor(std::move(type_ctor)),
          name(std::move(name)),
          attributes(std::move(attributes)) {}
    std::unique_ptr<TypeConstructor> type_ctor;
    SourceSpan name;
    std::unique_ptr<raw::AttributeList> attributes;
  };

  Resource(std::unique_ptr<raw::AttributeList> attributes, Name name,
           std::unique_ptr<TypeConstructor> subtype_ctor, std::vector<Property> properties)
      : Decl(Kind::kResource, std::move(attributes), std::move(name)),
        subtype_ctor(std::move(subtype_ctor)),
        properties(std::move(properties)) {}

  // Set during construction.
  std::unique_ptr<TypeConstructor> subtype_ctor;
  std::vector<Property> properties;
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
  TypeTemplate(Name name, Typespace* typespace, Reporter* reporter)
      : typespace_(typespace), name_(std::move(name)), reporter_(reporter) {}

  TypeTemplate(TypeTemplate&& type_template) = default;

  virtual ~TypeTemplate() = default;

  const Name& name() const { return name_; }

  struct CreateInvocation {
    const std::optional<SourceSpan>& span;
    const Type* arg_type;
    const std::optional<types::HandleSubtype>& handle_subtype;
    const Constant* handle_rights;
    const Size* size;
    const types::Nullability nullability;
  };
  virtual bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
                      std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const = 0;

 protected:
  bool Fail(const ErrorDef<const TypeTemplate*>& err, const std::optional<SourceSpan>& span) const;

  Typespace* typespace_;

  Name name_;

 private:
  Reporter* reporter_;
};

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
class Typespace {
 public:
  explicit Typespace(Reporter* reporter) : reporter_(reporter) {}

  bool Create(const flat::Name& name, const Type* arg_type,
              const std::optional<types::HandleSubtype>& handle_subtype,
              const Constant* handle_rights, const Size* size, types::Nullability nullability,
              const Type** out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias);

  void AddTemplate(std::unique_ptr<TypeTemplate> type_template);

  // RootTypes creates a instance with all primitive types. It is meant to be
  // used as the top-level types lookup mechanism, providing definitional
  // meaning to names such as `int64`, or `bool`.
  static Typespace RootTypes(Reporter* reporter);

 private:
  friend class TypeAliasTypeTemplate;

  bool CreateNotOwned(const flat::Name& name, const Type* arg_type,
                      const std::optional<types::HandleSubtype>& handle_subtype,
                      const Constant* handle_rights, const Size* size,
                      types::Nullability nullability, std::unique_ptr<Type>* out_type,
                      std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias);
  const TypeTemplate* LookupTemplate(const flat::Name& name) const;

  std::map<Name::Key, std::unique_ptr<TypeTemplate>> templates_;
  std::vector<std::unique_ptr<Type>> types_;

  Reporter* reporter_;
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
  using Constraint =
      fit::function<bool(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl)>;

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
    kResourceDecl,
    kResourceProperty,
    kServiceDecl,
    kServiceMember,
    kStructDecl,
    kStructMember,
    kTableDecl,
    kTableMember,
    kTypeAliasDecl,
    kUnionDecl,
    kUnionMember,
    kDeprecated,
  };

  AttributeSchema(const std::set<Placement>& allowed_placements,
                  const std::set<std::string> allowed_values,
                  Constraint constraint = NoOpConstraint);

  AttributeSchema(AttributeSchema&& schema) = default;

  static AttributeSchema Deprecated();

  void ValidatePlacement(Reporter* reporter, const raw::Attribute& attribute,
                         Placement placement) const;

  void ValidateValue(Reporter* reporter, const raw::Attribute& attribute) const;

  void ValidateConstraint(Reporter* reporter, const raw::Attribute& attribute,
                          const Decl* decl) const;

 private:
  static bool NoOpConstraint(Reporter* reporter, const raw::Attribute& attribute,
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

  const AttributeSchema* RetrieveAttributeSchema(Reporter* reporter,
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
  bool VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter);

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

class StepBase;
class ConsumeStep;
class CompileStep;
class VerifyResourcenessStep;
class VerifyAttributesStep;

using MethodHasher = fit::function<raw::Ordinal64(
    const std::vector<std::string_view>& library_name, const std::string_view& protocol_name,
    const std::string_view& selector_name, const raw::SourceElement& source_element)>;

class Library {
  friend StepBase;
  friend ConsumeStep;
  friend CompileStep;
  friend VerifyResourcenessStep;
  friend VerifyAttributesStep;

 public:
  Library(const Libraries* all_libraries, Reporter* reporter, Typespace* typespace,
          MethodHasher method_hasher, ExperimentalFlags experimental_flags)
      : all_libraries_(all_libraries),
        reporter_(reporter),
        typespace_(typespace),
        method_hasher_(std::move(method_hasher)),
        experimental_flags_(experimental_flags) {}

  bool ConsumeFile(std::unique_ptr<raw::File> file);
  bool Compile();

  const std::vector<std::string_view>& name() const { return library_name_; }
  const raw::AttributeList* attributes() const { return attributes_.get(); }

 private:
  bool Fail(std::unique_ptr<Diagnostic> err);
  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& err, const Args&... args);
  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
            const Args&... args);
  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& err, const Name& name, const Args&... args) {
    return Fail(err, name.span(), args...);
  }
  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& err, const Decl& decl, const Args&... args) {
    return Fail(err, decl.name, args...);
  }

  void ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                   const raw::AttributeList* attributes);
  void ValidateAttributesConstraints(const Decl* decl, const raw::AttributeList* attributes);

  // TODO(fxbug.dev/7920): Rationalize the use of names. Here, a simple name is
  // one that is not scoped, it is just text. An anonymous name is one that
  // is guaranteed to be unique within the library, and a derived name is one
  // that is library scoped but derived from the concatenated components using
  // underscores as delimiters.
  SourceSpan GeneratedSimpleName(const std::string& name);
  std::string NextAnonymousName();

  // Attempts to compile a compound identifier, and resolve it to a name
  // within the context of a library. On success, the name is returned.
  // On failure, no name is returned, and a failure is emitted, i.e. the
  // caller is not responsible for reporting the resolution error.
  std::optional<Name> CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier);
  bool RegisterDecl(std::unique_ptr<Decl> decl);

  ConsumeStep StartConsumeStep();
  CompileStep StartCompileStep();
  VerifyResourcenessStep StartVerifyResourcenessStep();
  VerifyAttributesStep StartVerifyAttributesStep();

  bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant,
                       std::unique_ptr<Constant>* out_constant);
  bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor, SourceSpan span,
                              std::unique_ptr<TypeConstructor>* out_type);

  void ConsumeUsing(std::unique_ptr<raw::Using> using_directive);
  bool ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive);
  void ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration);
  void ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);
  void ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration);
  void ConsumeProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration);
  bool ConsumeResourceDeclaration(std::unique_ptr<raw::ResourceDeclaration> resource_declaration);
  bool ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                            bool anonymous, Struct** out_struct_decl);
  bool CreateMethodResult(const Name& protocol_name, SourceSpan response_span,
                          raw::ProtocolMethod* method, Struct* in_response, Struct** out_response);
  void ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl);
  void ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration);
  void ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration);
  void ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration);

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
  bool CompileResource(Resource* resource_declaration);
  bool CompileService(Service* service_decl);
  bool CompileStruct(Struct* struct_declaration);
  bool CompileTable(Table* table_declaration);
  bool CompileUnion(Union* union_declaration);
  bool CompileTypeAlias(TypeAlias* type_alias);

  // Compiling a type validates the type: in particular, we validate that optional identifier types
  // refer to things that can in fact be nullable (ie not enums).
  bool CompileTypeConstructor(TypeConstructor* type);

  ConstantValue::Kind ConstantValuePrimitiveKind(const types::PrimitiveSubtype primitive_subtype);
  bool ResolveHandleSubtypeIdentifier(TypeConstructor* type_ctor, types::HandleSubtype* subtype);
  bool ResolveSizeBound(TypeConstructor* type_ctor, const Size** out_size);
  bool ResolveOrOperatorConstant(Constant* constant, const Type* type,
                                 const ConstantValue& left_operand,
                                 const ConstantValue& right_operand);
  bool ResolveConstant(Constant* constant, const Type* type);
  bool ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type);
  bool ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type);

  // Validates a single member of a bits or enum. On success, returns nullptr,
  // and on failure returns an error.
  template <typename MemberType>
  using MemberValidator = fit::function<std::unique_ptr<Diagnostic>(
      const MemberType& member, const raw::AttributeList* attributes)>;
  template <typename DeclType, typename MemberType>
  bool ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator);
  template <typename MemberType>
  bool ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask);
  template <typename MemberType>
  bool ValidateEnumMembersAndCalcUnknownValue(Enum* enum_decl, MemberType* out_unknown_value);

  void VerifyDeclAttributes(Decl* decl);
  bool VerifyInlineSize(const Struct* decl);

 public:
  bool CompileDecl(Decl* decl);

  // Returns nullptr when the |name| cannot be resolved to a
  // Name. Otherwise it returns the declaration.
  Decl* LookupDeclByName(Name::Key name) const;

  template <typename NumericType>
  bool ParseNumericLiteral(const raw::NumericLiteral* literal, NumericType* out_value) const;

  bool HasAttribute(std::string_view name) const;

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
  std::vector<Decl*> declaration_order_;

 private:
  // TODO(fxbug.dev/7724): Remove when canonicalizing types.
  const Name kSizeTypeName = Name::CreateIntrinsic("uint32");
  const PrimitiveType kSizeType = PrimitiveType(kSizeTypeName, types::PrimitiveSubtype::kUint32);

  const Name kRightsTypeName = Name::CreateIntrinsic("uint32");
  const PrimitiveType kRightsType =
      PrimitiveType(kRightsTypeName, types::PrimitiveSubtype::kUint32);

  const Name kHandleSubtypeTypeName = Name::CreateIntrinsic("uint32");
  const PrimitiveType kHandleSubtypeType =
      PrimitiveType(kHandleSubtypeTypeName, types::PrimitiveSubtype::kUint32);

  std::unique_ptr<raw::AttributeList> attributes_;

  Dependencies dependencies_;
  const Libraries* all_libraries_;

  // All Decl pointers here are non-null. They are owned by the various
  // foo_declarations_ members of this Library object, or of one of the Library
  // objects in dependencies_.
  std::map<Name::Key, Decl*> declarations_;

  // This map contains a subset of declarations_ (no imported declarations)
  // keyed by `utils::canonicalize(name.decl_name())` rather than `name.key()`.
  std::map<std::string, Decl*> declarations_by_canonical_name_;

  Reporter* reporter_;
  Typespace* typespace_;
  const MethodHasher method_hasher_;
  const ExperimentalFlags experimental_flags_;

  uint32_t anon_counter_ = 0;

  VirtualSourceFile generated_source_file_{"generated"};
};

class StepBase {
 public:
  StepBase(Library* library)
      : library_(library), checkpoint_(library->reporter_->Checkpoint()), done_(false) {}

  ~StepBase() { assert(done_ && "Step must be completed before destructor is called"); }

  bool Done() {
    done_ = true;
    return checkpoint_.NoNewErrors();
  }

 protected:
  Library* library_;  // link to library for which this step was created

 private:
  Reporter::Counts checkpoint_;
  bool done_;
};

class ConsumeStep : public StepBase {
 public:
  ConsumeStep(Library* library) : StepBase(library) {}

  void ForUsing(std::unique_ptr<raw::Using> using_directive) {
    library_->ConsumeUsing(std::move(using_directive));
  }
  void ForBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
    library_->ConsumeBitsDeclaration(std::move(bits_declaration));
  }
  void ForConstDeclaration(std::unique_ptr<raw::ConstDeclaration> bits_declaration) {
    library_->ConsumeConstDeclaration(std::move(bits_declaration));
  }
  void ForEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
    library_->ConsumeEnumDeclaration(std::move(enum_declaration));
  }
  void ForProtocolDeclaration(std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration) {
    library_->ConsumeProtocolDeclaration(std::move(protocol_declaration));
  }
  void ForResourceDeclaration(std::unique_ptr<raw::ResourceDeclaration> resource_declaration) {
    library_->ConsumeResourceDeclaration(std::move(resource_declaration));
  }
  void ForServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl) {
    library_->ConsumeServiceDeclaration(std::move(service_decl));
  }
  void ForStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
    library_->ConsumeStructDeclaration(std::move(struct_declaration));
  }
  void ForTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration) {
    library_->ConsumeTableDeclaration(std::move(table_declaration));
  }
  void ForUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
    library_->ConsumeUnionDeclaration(std::move(union_declaration));
  }
};

class CompileStep : public StepBase {
 public:
  CompileStep(Library* library) : StepBase(library) {}

  void ForDecl(Decl* decl) { library_->CompileDecl(decl); }
};

class VerifyResourcenessStep : public StepBase {
 public:
  VerifyResourcenessStep(Library* library) : StepBase(library) {}

  void ForDecl(const Decl* decl);

 private:
  // Returns the effective resourcenss of |type|. The set of effective resource
  // types includes (1) nominal resource types per the FTP-057 definition, and
  // (2) declarations that have an effective resource member (or equivalently,
  // transitively contain a nominal resource).
  types::Resourceness EffectiveResourceness(const Type* type);

  // Map from struct/table/union declarations to their effective resourceness. A
  // value of std::nullopt indicates that the declaration has been visited, used
  // to prevent infinite recursion.
  std::map<const Decl*, std::optional<types::Resourceness>> effective_resourceness_;
};

class VerifyAttributesStep : public StepBase {
 public:
  VerifyAttributesStep(Library* library) : StepBase(library) {}

  void ForDecl(Decl* decl) { library_->VerifyDeclAttributes(decl); }
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

inline std::any Protocol::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }

}  // namespace flat
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FLAT_AST_H_
