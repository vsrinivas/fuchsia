// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>
#include <sstream>
#include <utility>

#include "fidl/attributes.h"
#include "fidl/diagnostics.h"
#include "fidl/lexer.h"
#include "fidl/names.h"
#include "fidl/ordinals.h"
#include "fidl/parser.h"
#include "fidl/raw_ast.h"
#include "fidl/types.h"
#include "fidl/utils.h"

namespace fidl {
namespace flat {

using namespace diagnostics;

namespace {

constexpr uint32_t kHandleSameRights = 0x80000000;  // ZX_HANDLE_SAME_RIGHTS

class ScopeInsertResult {
 public:
  explicit ScopeInsertResult(std::unique_ptr<SourceSpan> previous_occurrence)
      : previous_occurrence_(std::move(previous_occurrence)) {}

  static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
  static ScopeInsertResult FailureAt(SourceSpan previous) {
    return ScopeInsertResult(std::make_unique<SourceSpan>(previous));
  }

  bool ok() const { return previous_occurrence_ == nullptr; }

  const SourceSpan& previous_occurrence() const {
    assert(!ok());
    return *previous_occurrence_;
  }

 private:
  std::unique_ptr<SourceSpan> previous_occurrence_;
};

template <typename T>
class Scope {
 public:
  ScopeInsertResult Insert(const T& t, SourceSpan span) {
    auto iter = scope_.find(t);
    if (iter != scope_.end()) {
      return ScopeInsertResult::FailureAt(iter->second);
    } else {
      scope_.emplace(t, span);
      return ScopeInsertResult::Ok();
    }
  }

  typename std::map<T, SourceSpan>::const_iterator begin() const { return scope_.begin(); }

  typename std::map<T, SourceSpan>::const_iterator end() const { return scope_.end(); }

 private:
  std::map<T, SourceSpan> scope_;
};

using Ordinal64Scope = Scope<uint64_t>;

std::optional<std::pair<uint64_t, SourceSpan>> FindFirstNonDenseOrdinal(
    const Ordinal64Scope& scope) {
  uint64_t last_ordinal_seen = 0;
  for (const auto& ordinal_and_loc : scope) {
    uint64_t next_expected_ordinal = last_ordinal_seen + 1;
    if (ordinal_and_loc.first != next_expected_ordinal) {
      return std::optional{std::make_pair(next_expected_ordinal, ordinal_and_loc.second)};
    }
    last_ordinal_seen = ordinal_and_loc.first;
  }
  return std::nullopt;
}

struct MethodScope {
  Ordinal64Scope ordinals;
  Scope<std::string> canonical_names;
  Scope<const Protocol*> protocols;
};

// A helper class to track when a Decl is compiling and compiled.
class Compiling {
 public:
  explicit Compiling(Decl* decl) : decl_(decl) { decl_->compiling = true; }

  ~Compiling() {
    decl_->compiling = false;
    decl_->compiled = true;
  }

 private:
  Decl* decl_;
};

template <typename T>
std::unique_ptr<Diagnostic> ValidateUnknownConstraints(const Decl& decl,
                                                       types::Strictness decl_strictness,
                                                       const std::vector<const T*>* members) {
  if (!members)
    return nullptr;

  const bool is_transitional = decl.HasAttribute("Transitional");

  const bool is_strict = [&] {
    switch (decl_strictness) {
      case types::Strictness::kStrict:
        return true;
      case types::Strictness::kFlexible:
        return false;
    }
  }();

  bool found_member = false;
  for (const auto* member : *members) {
    const bool has_unknown = member->attributes && member->attributes->HasAttribute("Unknown");
    if (!has_unknown)
      continue;

    if (is_strict && !is_transitional) {
      return Reporter::MakeError(ErrUnknownAttributeOnInvalidType, member->name);
    }

    if (found_member) {
      return Reporter::MakeError(ErrUnknownAttributeOnMultipleMembers, member->name);
    }

    found_member = true;
  }

  return nullptr;
}

}  // namespace

uint32_t PrimitiveType::SubtypeSize(types::PrimitiveSubtype subtype) {
  switch (subtype) {
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kUint8:
      return 1u;

    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kUint16:
      return 2u;

    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kUint32:
      return 4u;

    case types::PrimitiveSubtype::kFloat64:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kUint64:
      return 8u;
  }
}

bool Decl::HasAttribute(std::string_view name) const {
  if (!attributes)
    return false;
  return attributes->HasAttribute(std::string(name));
}

std::string_view Decl::GetAttribute(std::string_view name) const {
  if (!attributes)
    return std::string_view();
  for (const auto& attribute : attributes->attributes) {
    if (attribute.name == name) {
      if (attribute.value != "") {
        const auto& value = attribute.value;
        return std::string_view(value.data(), value.size());
      }
      // Don't search for another attribute with the same name.
      break;
    }
  }
  return std::string_view();
}

std::string Decl::GetName() const { return std::string(name.decl_name()); }

const std::set<std::pair<std::string, std::string_view>> allowed_simple_unions{{
    {"fuchsia.io", "NodeInfo"},
}};

bool IsSimple(const Type* type, Reporter* reporter) {
  auto depth = fidl::OldWireFormatDepth(type);
  switch (type->kind) {
    case Type::Kind::kVector: {
      auto vector_type = static_cast<const VectorType*>(type);
      if (*vector_type->element_count == Size::Max())
        return false;
      switch (vector_type->element_type->kind) {
        case Type::Kind::kHandle:
        case Type::Kind::kRequestHandle:
        case Type::Kind::kPrimitive:
          return true;
        case Type::Kind::kArray:
        case Type::Kind::kVector:
        case Type::Kind::kString:
        case Type::Kind::kIdentifier:
          return false;
      }
    }
    case Type::Kind::kString: {
      auto string_type = static_cast<const StringType*>(type);
      return *string_type->max_size < Size::Max();
    }
    case Type::Kind::kArray:
    case Type::Kind::kHandle:
    case Type::Kind::kRequestHandle:
    case Type::Kind::kPrimitive:
      return depth == 0u;
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      if (identifier_type->type_decl->kind == Decl::Kind::kUnion) {
        auto union_name = std::make_pair<const std::string&, const std::string_view&>(
            LibraryName(identifier_type->name.library(), "."), identifier_type->name.decl_name());
        if (allowed_simple_unions.find(union_name) == allowed_simple_unions.end()) {
          // Any unions not in the allow-list are treated as non-simple.
          reporter->ReportError(ErrUnionCannotBeSimple, identifier_type->name.span(),
                                identifier_type->name);
          return false;
        }
      }
      switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
          // If the identifier is nullable, then we can handle a depth of 1
          // because the secondary object is directly accessible.
          return depth <= 1u;
        case types::Nullability::kNonnullable:
          return depth == 0u;
      }
    }
  }
}

// Returns true if |type| is a resource type, false if it is a value type. See
// FTP-057 for the definitions of these terms.
bool IsResourceType(const Type* type) {
  switch (type->kind) {
    case Type::Kind::kPrimitive:
    case Type::Kind::kString:
      return false;
    case Type::Kind::kHandle:
    case Type::Kind::kRequestHandle:
      return true;
    case Type::Kind::kArray:
      return IsResourceType(static_cast<const ArrayType*>(type)->element_type);
    case Type::Kind::kVector:
      return IsResourceType(static_cast<const VectorType*>(type)->element_type);
    case Type::Kind::kIdentifier: {
      const auto decl = static_cast<const IdentifierType*>(type)->type_decl;
      switch (decl->kind) {
        case Decl::Kind::kBits:
        case Decl::Kind::kEnum:
          return false;
        case Decl::Kind::kProtocol:
          return true;
        case Decl::Kind::kStruct:
          return static_cast<const Struct*>(decl)->resourceness == types::Resourceness::kResource;
        case Decl::Kind::kTable:
          return static_cast<const Table*>(decl)->resourceness == types::Resourceness::kResource;
        case Decl::Kind::kUnion:
          return static_cast<const Union*>(decl)->resourceness == types::Resourceness::kResource;
        case Decl::Kind::kConst:
        case Decl::Kind::kResource:
        case Decl::Kind::kService:
        case Decl::Kind::kTypeAlias:
          assert(false && "Unexpected kind");
      }
    }
  }
  __builtin_unreachable();
}

FieldShape Struct::Member::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Table::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

FieldShape Union::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

std::vector<std::reference_wrapper<const Union::Member>> Union::MembersSortedByXUnionOrdinal()
    const {
  std::vector<std::reference_wrapper<const Member>> sorted_members(members.cbegin(),
                                                                   members.cend());
  std::sort(sorted_members.begin(), sorted_members.end(),
            [](const auto& member1, const auto& member2) {
              return member1.get().ordinal->value < member2.get().ordinal->value;
            });
  return sorted_members;
}

bool Typespace::Create(const flat::Name& name, const Type* arg_type,
                       const std::optional<types::HandleSubtype>& handle_subtype,
                       const Constant* handle_rights, const Size* size,
                       types::Nullability nullability, const Type** out_type,
                       std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) {
  std::unique_ptr<Type> type;
  if (!CreateNotOwned(name, arg_type, handle_subtype, handle_rights, size, nullability, &type,
                      out_from_type_alias))
    return false;
  types_.push_back(std::move(type));
  *out_type = types_.back().get();
  return true;
}

bool Typespace::CreateNotOwned(const flat::Name& name, const Type* arg_type,
                               const std::optional<types::HandleSubtype>& handle_subtype,
                               const Constant* handle_rights, const Size* size,
                               types::Nullability nullability, std::unique_ptr<Type>* out_type,
                               std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) {
  // TODO(pascallouis): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name,
  // arg_type, size, and nullability.

  auto type_template = LookupTemplate(name);
  if (type_template == nullptr) {
    reporter_->ReportError(ErrUnknownType, name.span(), name);
    return false;
  }
  return type_template->Create({.span = name.span(),
                                .arg_type = arg_type,
                                .handle_subtype = handle_subtype,
                                .handle_rights = handle_rights,
                                .size = size,
                                .nullability = nullability},
                               out_type, out_from_type_alias);
}

void Typespace::AddTemplate(std::unique_ptr<TypeTemplate> type_template) {
  templates_.emplace(type_template->name(), std::move(type_template));
}

const TypeTemplate* Typespace::LookupTemplate(const flat::Name& name) const {
  auto global_name = Name::Key(nullptr, name.decl_name());
  if (auto iter = templates_.find(global_name); iter != templates_.end()) {
    return iter->second.get();
  }

  if (auto iter = templates_.find(name); iter != templates_.end()) {
    return iter->second.get();
  }

  return nullptr;
}

bool TypeTemplate::Fail(const ErrorDef<const TypeTemplate*>& err,
                        const std::optional<SourceSpan>& span) const {
  reporter_->ReportError(err, span, this);
  return false;
}

class PrimitiveTypeTemplate : public TypeTemplate {
 public:
  PrimitiveTypeTemplate(Typespace* typespace, Reporter* reporter, const std::string& name,
                        types::PrimitiveSubtype subtype)
      : TypeTemplate(Name::CreateIntrinsic(name), typespace, reporter), subtype_(subtype) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args.span);
    if (args.size != nullptr)
      return Fail(ErrCannotHaveSize, args.span);
    if (args.nullability == types::Nullability::kNullable)
      return Fail(ErrCannotBeNullable, args.span);

    *out_type = std::make_unique<PrimitiveType>(name_, subtype_);
    return true;
  }

 private:
  const types::PrimitiveSubtype subtype_;
};

class BytesTypeTemplate final : public TypeTemplate {
 public:
  BytesTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter),
        uint8_type_(kUint8Type) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args.span);
    const Size* size = args.size;
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<VectorType>(name_, &uint8_type_, size, args.nullability);
    return true;
  }

 private:
  // TODO(FIDL-389): Remove when canonicalizing types.
  const Name kUint8TypeName = Name::CreateIntrinsic("uint8");
  const PrimitiveType kUint8Type = PrimitiveType(kUint8TypeName, types::PrimitiveSubtype::kUint8);

  const PrimitiveType uint8_type_;
  Size max_size = Size::Max();
};

class ArrayTypeTemplate final : public TypeTemplate {
 public:
  ArrayTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("array"), typespace, reporter) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args.span);
    if (args.size == nullptr)
      return Fail(ErrMustHaveSize, args.span);
    if (args.size->value == 0)
      return Fail(ErrMustHaveNonZeroSize, args.span);
    if (args.nullability == types::Nullability::kNullable)
      return Fail(ErrCannotBeNullable, args.span);

    *out_type = std::make_unique<ArrayType>(name_, args.arg_type, args.size);
    return true;
  }
};

class VectorTypeTemplate final : public TypeTemplate {
 public:
  VectorTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args.span);
    const Size* size = args.size;
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<VectorType>(name_, args.arg_type, size, args.nullability);
    return true;
  }

 private:
  Size max_size = Size::Max();
};

class StringTypeTemplate final : public TypeTemplate {
 public:
  StringTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("string"), typespace, reporter) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args.span);
    const Size* size = args.size;
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<StringType>(name_, size, args.nullability);
    return true;
  }

 private:
  Size max_size = Size::Max();
};

class HandleTypeTemplate final : public TypeTemplate {
 public:
  HandleTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("handle"), typespace, reporter) {
    same_rights = std::make_unique<Constant>(Constant::Kind::kSynthesized, SourceSpan());
    same_rights->ResolveTo(std::make_unique<NumericConstantValue<uint32_t>>(kHandleSameRights));
  }

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(args.arg_type == nullptr);

    if (args.size != nullptr)
      return Fail(ErrCannotHaveSize, args.span);

    auto handle_subtype = args.handle_subtype.value_or(types::HandleSubtype::kHandle);
    const Constant* handle_rights = args.handle_rights;
    if (handle_rights == nullptr)
      handle_rights = same_rights.get();

    *out_type =
        std::make_unique<HandleType>(name_, handle_subtype, handle_rights, args.nullability);
    return true;
  }

 private:
  std::unique_ptr<Constant> same_rights;
};

class RequestTypeTemplate final : public TypeTemplate {
 public:
  RequestTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("request"), typespace, reporter) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (args.arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args.span);
    if (args.arg_type->kind != Type::Kind::kIdentifier)
      return Fail(ErrMustBeAProtocol, args.span);
    auto protocol_type = static_cast<const IdentifierType*>(args.arg_type);
    if (protocol_type->type_decl->kind != Decl::Kind::kProtocol)
      return Fail(ErrMustBeAProtocol, args.span);
    if (args.size != nullptr)
      return Fail(ErrCannotHaveSize, args.span);

    *out_type = std::make_unique<RequestHandleType>(name_, protocol_type, args.nullability);
    return true;
  }

 private:
  // TODO(pascallouis): Make Min/Max an actual value on NumericConstantValue
  // class, to simply write &Size::Max() above.
  Size max_size = Size::Max();
};

class TypeDeclTypeTemplate final : public TypeTemplate {
 public:
  TypeDeclTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, Library* library,
                       TypeDecl* type_decl)
      : TypeTemplate(std::move(name), typespace, reporter),
        library_(library),
        type_decl_(type_decl) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);

    if (!type_decl_->compiled && type_decl_->kind != Decl::Kind::kProtocol) {
      if (type_decl_->compiling) {
        type_decl_->recursive = true;
      } else {
        if (!library_->CompileDecl(type_decl_)) {
          return false;
        }
      }
    }
    switch (type_decl_->kind) {
      case Decl::Kind::kService:
        return Fail(ErrCannotUseServicesInOtherDeclarations, args.span);

      case Decl::Kind::kProtocol:
        break;

      case Decl::Kind::kUnion:
        // Do nothing here: nullable Unions have the same encoding
        // representation as non-optional Unions (i.e. nullable Unions are
        // inlined).
        break;

      case Decl::Kind::kEnum:
      case Decl::Kind::kTable:
        if (args.nullability == types::Nullability::kNullable)
          return Fail(ErrCannotBeNullable, args.span);
        break;

      case Decl::Kind::kResource: {
        // Currently the only resource types are new-style handles,
        // and they should be resolved to concrete subtypes and
        // dispatched to the handle template earlier.
        assert(false);
        break;
      }

      default:
        if (args.nullability == types::Nullability::kNullable)
          break;
    }

    *out_type = std::make_unique<IdentifierType>(name_, args.nullability, type_decl_);
    return true;
  }

 private:
  Library* library_;
  TypeDecl* type_decl_;
};

class TypeAliasTypeTemplate final : public TypeTemplate {
 public:
  TypeAliasTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, Library* library,
                        TypeAlias* decl)
      : TypeTemplate(std::move(name), typespace, reporter), library_(library), decl_(decl) {}

  bool Create(const CreateInvocation& args, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!args.handle_subtype);
    assert(!args.handle_rights);

    if (!decl_->compiled) {
      assert(!decl_->compiling && "TODO(fxbug.dev/35218): Improve support for recursive types.");

      if (!library_->CompileDecl(decl_)) {
        return false;
      }
    }

    const Type* arg_type = nullptr;
    if (decl_->partial_type_ctor->maybe_arg_type_ctor) {
      if (args.arg_type) {
        return Fail(ErrCannotParametrizeTwice, args.span);
      }
      arg_type = decl_->partial_type_ctor->maybe_arg_type_ctor->type;
    } else {
      arg_type = args.arg_type;
    }

    const Size* size = nullptr;
    if (decl_->partial_type_ctor->maybe_size) {
      if (args.size) {
        return Fail(ErrCannotBoundTwice, args.span);
      }
      size = static_cast<const Size*>(&decl_->partial_type_ctor->maybe_size->Value());
    } else {
      size = args.size;
    }

    types::Nullability nullability;
    if (decl_->partial_type_ctor->nullability == types::Nullability::kNullable) {
      if (args.nullability == types::Nullability::kNullable) {
        return Fail(ErrCannotIndicateNullabilityTwice, args.span);
      }
      nullability = types::Nullability::kNullable;
    } else {
      nullability = args.nullability;
    }

    if (!typespace_->CreateNotOwned(decl_->partial_type_ctor->name, arg_type,
                                    // TODO(pascallouis): Oops, that's wrong. Need to pass handle
                                    // parametrization down.
                                    std::optional<types::HandleSubtype>(),
                                    decl_->partial_type_ctor->handle_rights.get(), size,
                                    nullability, out_type, nullptr))
      return false;
    if (out_from_type_alias)
      *out_from_type_alias =
          TypeConstructor::FromTypeAlias(decl_, args.arg_type, args.size, args.nullability);
    return true;
  }

 private:
  Library* library_;
  TypeAlias* decl_;
};

Typespace Typespace::RootTypes(Reporter* reporter) {
  Typespace root_typespace(reporter);

  auto add_template = [&](std::unique_ptr<TypeTemplate> type_template) {
    const Name& name = type_template->name();
    root_typespace.templates_.emplace(name, std::move(type_template));
  };

  auto add_primitive = [&](const std::string& name, types::PrimitiveSubtype subtype) {
    add_template(std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, name, subtype));
  };

  add_primitive("bool", types::PrimitiveSubtype::kBool);

  add_primitive("int8", types::PrimitiveSubtype::kInt8);
  add_primitive("int16", types::PrimitiveSubtype::kInt16);
  add_primitive("int32", types::PrimitiveSubtype::kInt32);
  add_primitive("int64", types::PrimitiveSubtype::kInt64);
  add_primitive("uint8", types::PrimitiveSubtype::kUint8);
  add_primitive("uint16", types::PrimitiveSubtype::kUint16);
  add_primitive("uint32", types::PrimitiveSubtype::kUint32);
  add_primitive("uint64", types::PrimitiveSubtype::kUint64);

  add_primitive("float32", types::PrimitiveSubtype::kFloat32);
  add_primitive("float64", types::PrimitiveSubtype::kFloat64);

  // TODO(FIDL-483): Remove when there is generalized support.
  const static auto kByteName = Name::CreateIntrinsic("byte");
  const static auto kBytesName = Name::CreateIntrinsic("bytes");
  root_typespace.templates_.emplace(
      kByteName, std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, "uint8",
                                                         types::PrimitiveSubtype::kUint8));
  root_typespace.templates_.emplace(kBytesName,
                                    std::make_unique<BytesTypeTemplate>(&root_typespace, reporter));

  add_template(std::make_unique<ArrayTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<VectorTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<StringTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<HandleTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<RequestTypeTemplate>(&root_typespace, reporter));

  return root_typespace;
}

AttributeSchema::AttributeSchema(const std::set<Placement>& allowed_placements,
                                 const std::set<std::string> allowed_values, Constraint constraint)
    : allowed_placements_(allowed_placements),
      allowed_values_(allowed_values),
      constraint_(std::move(constraint)) {}

AttributeSchema AttributeSchema::Deprecated() {
  return AttributeSchema({Placement::kDeprecated}, {});
}

void AttributeSchema::ValidatePlacement(Reporter* reporter, const raw::Attribute& attribute,
                                        Placement placement) const {
  if (allowed_placements_.size() == 0)
    return;

  if (allowed_placements_.size() == 1 && *allowed_placements_.cbegin() == Placement::kDeprecated) {
    reporter->ReportError(ErrDeprecatedAttribute, attribute.span(), attribute);
    return;
  }

  auto iter = allowed_placements_.find(placement);
  if (iter != allowed_placements_.end())
    return;
  reporter->ReportError(ErrInvalidAttributePlacement, attribute.span(), attribute);
}

void AttributeSchema::ValidateValue(Reporter* reporter, const raw::Attribute& attribute) const {
  if (allowed_values_.size() == 0)
    return;
  auto iter = allowed_values_.find(attribute.value);
  if (iter != allowed_values_.end())
    return;
  reporter->ReportError(ErrInvalidAttributeValue, attribute.span(), attribute, attribute.value,
                        allowed_values_);
}

void AttributeSchema::ValidateConstraint(Reporter* reporter, const raw::Attribute& attribute,
                                         const Decl* decl) const {
  auto check = reporter->Checkpoint();
  auto passed = constraint_(reporter, attribute, decl);
  if (passed) {
    assert(check.NoNewErrors() && "cannot add errors and pass");
  } else if (check.NoNewErrors()) {
    // TODO(pascallouis): It would be nicer to use the span of
    // the declaration, however we do not keep it around today.
    reporter->ReportError(ErrAttributeConstraintNotSatisfied, attribute.span(), attribute,
                          attribute.value);
  }
}

bool SimpleLayoutConstraint(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl) {
  assert(decl->kind == Decl::Kind::kStruct);
  auto struct_decl = static_cast<const Struct*>(decl);
  bool ok = true;
  for (const auto& member : struct_decl->members) {
    if (!IsSimple(member.type_ctor.get()->type, reporter)) {
      reporter->ReportError(ErrMemberMustBeSimple, member.name, member.name.data());
      ok = false;
    }
  }
  return ok;
}

bool ParseBound(Reporter* reporter, const SourceSpan& span, const std::string& input,
                uint32_t* out_value) {
  auto result = utils::ParseNumeric(input, out_value, 10);
  switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
      reporter->ReportError(ErrBoundIsTooBig, span);
      return false;
    case utils::ParseNumericResult::kMalformed: {
      reporter->ReportError(ErrUnableToParseBound, span, input);
      return false;
    }
    case utils::ParseNumericResult::kSuccess:
      return true;
  }
}

bool MaxBytesConstraint(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl) {
  uint32_t bound;
  if (!ParseBound(reporter, attribute.span(), attribute.value, &bound))
    return false;
  uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      max_bytes = struct_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  struct_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      max_bytes = table_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  table_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      max_bytes = union_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  union_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_bytes > bound) {
    reporter->ReportError(ErrTooManyBytes, attribute.span(), bound, max_bytes);
    return false;
  }
  return true;
}

bool MaxHandlesConstraint(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl) {
  uint32_t bound;
  if (!ParseBound(reporter, attribute.span(), attribute.value, &bound))
    return false;
  uint32_t max_handles = std::numeric_limits<uint32_t>::max();
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      max_handles = struct_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      max_handles = table_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      max_handles = union_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_handles > bound) {
    reporter->ReportError(ErrTooManyHandles, attribute.span(), bound, max_handles);
    return false;
  }
  return true;
}

bool ResultShapeConstraint(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl) {
  assert(decl->kind == Decl::Kind::kUnion);
  auto union_decl = static_cast<const Union*>(decl);
  assert(union_decl->members.size() == 2);
  auto& error_member = union_decl->members.at(1);
  assert(error_member.maybe_used && "must have an error member");
  auto error_type = error_member.maybe_used->type_ctor->type;

  const PrimitiveType* error_primitive = nullptr;
  if (error_type->kind == Type::Kind::kPrimitive) {
    error_primitive = static_cast<const PrimitiveType*>(error_type);
  } else if (error_type->kind == Type::Kind::kIdentifier) {
    auto identifier_type = static_cast<const IdentifierType*>(error_type);
    if (identifier_type->type_decl->kind == Decl::Kind::kEnum) {
      auto error_enum = static_cast<const Enum*>(identifier_type->type_decl);
      assert(error_enum->subtype_ctor->type->kind == Type::Kind::kPrimitive);
      error_primitive = static_cast<const PrimitiveType*>(error_enum->subtype_ctor->type);
    }
  }

  if (!error_primitive || (error_primitive->subtype != types::PrimitiveSubtype::kInt32 &&
                           error_primitive->subtype != types::PrimitiveSubtype::kUint32)) {
    reporter->ReportError(ErrInvalidErrorType, decl->name.span());
    return false;
  }

  return true;
}

static std::string Trim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !utils::IsWhitespace(static_cast<char>(ch));
          }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](int ch) { return !utils::IsWhitespace(static_cast<char>(ch)); })
              .base(),
          s.end());
  return s;
}

bool TransportConstraint(Reporter* reporter, const raw::Attribute& attribute, const Decl* decl) {
  // Parse comma separated transports
  const std::string& value = attribute.value;
  std::string::size_type prev_pos = 0;
  std::string::size_type pos;
  std::vector<std::string> transports;
  while ((pos = value.find(',', prev_pos)) != std::string::npos) {
    transports.emplace_back(Trim(value.substr(prev_pos, pos - prev_pos)));
    prev_pos = pos + 1;
  }
  transports.emplace_back(Trim(value.substr(prev_pos)));

  // Validate that they're ok

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kValidTransports = new std::set<std::string>{
      "Channel",
      "Syscall",
  };
  for (auto transport : transports) {
    if (kValidTransports->count(transport) == 0) {
      reporter->ReportError(ErrInvalidTransportType, decl->name.span(), transport,
                            *kValidTransports);
      return false;
    }
  }
  return true;
}

Libraries::Libraries() {
  // clang-format off
  AddAttributeSchema("Discoverable", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
  }, {
    "",
  }));
  AddAttributeSchema("Doc", AttributeSchema({
    /* any placement */
  }, {
    /* any value */
  }));
  AddAttributeSchema("Layout", AttributeSchema::Deprecated()),
  AddAttributeSchema("ForDeprecatedCBindings", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
    AttributeSchema::Placement::kStructDecl,
  }, {
    "",
  },
  SimpleLayoutConstraint));
  AddAttributeSchema("MaxBytes", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
    AttributeSchema::Placement::kMethod,
    AttributeSchema::Placement::kStructDecl,
    AttributeSchema::Placement::kTableDecl,
    AttributeSchema::Placement::kUnionDecl,
  }, {
      /* any value */
  },
  MaxBytesConstraint));
  AddAttributeSchema("MaxHandles", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
    AttributeSchema::Placement::kMethod,
    AttributeSchema::Placement::kStructDecl,
    AttributeSchema::Placement::kTableDecl,
    AttributeSchema::Placement::kUnionDecl,
  }, {
    /* any value */
  },
  MaxHandlesConstraint));
  AddAttributeSchema("Result", AttributeSchema({
    AttributeSchema::Placement::kUnionDecl,
  }, {
      "",
  },
  ResultShapeConstraint));
  AddAttributeSchema("Selector", AttributeSchema({
    AttributeSchema::Placement::kMethod,
  }, {
      /* any value */
  }));
  AddAttributeSchema("Transitional", AttributeSchema({
    AttributeSchema::Placement::kMethod,
    AttributeSchema::Placement::kBitsDecl,
    AttributeSchema::Placement::kEnumDecl,
    AttributeSchema::Placement::kUnionDecl,
  }, {
    /* any value */
  }));
  AddAttributeSchema("Transport", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
  }, {
    /* any value */
  }, TransportConstraint));
  AddAttributeSchema("Unknown", AttributeSchema({
    AttributeSchema::Placement::kEnumMember,
    AttributeSchema::Placement::kUnionMember,
  }, {
    ""
  }));
  // clang-format on
}

bool Libraries::Insert(std::unique_ptr<Library> library) {
  std::vector<std::string_view> library_name = library->name();
  auto iter = all_libraries_.emplace(library_name, std::move(library));
  return iter.second;
}

bool Libraries::Lookup(const std::vector<std::string_view>& library_name,
                       Library** out_library) const {
  auto iter = all_libraries_.find(library_name);
  if (iter == all_libraries_.end()) {
    return false;
  }

  *out_library = iter->second.get();
  return true;
}

std::set<std::vector<std::string_view>> Libraries::Unused(const Library* target_library) const {
  std::set<std::vector<std::string_view>> unused;
  for (auto& name_library : all_libraries_)
    unused.insert(name_library.first);
  unused.erase(target_library->name());
  std::set<const Library*> worklist = {target_library};
  while (worklist.size() != 0) {
    auto it = worklist.begin();
    auto next = *it;
    worklist.erase(it);
    for (const auto dependency : next->dependencies()) {
      unused.erase(dependency->name());
      worklist.insert(dependency);
    }
  }
  return unused;
}

size_t EditDistance(const std::string& sequence1, const std::string& sequence2) {
  size_t s1_length = sequence1.length();
  size_t s2_length = sequence2.length();
  size_t row1[s1_length + 1];
  size_t row2[s1_length + 1];
  size_t* last_row = row1;
  size_t* this_row = row2;
  for (size_t i = 0; i <= s1_length; i++)
    last_row[i] = i;
  for (size_t j = 0; j < s2_length; j++) {
    this_row[0] = j + 1;
    auto s2c = sequence2[j];
    for (size_t i = 1; i <= s1_length; i++) {
      auto s1c = sequence1[i - 1];
      this_row[i] = std::min(std::min(last_row[i] + 1, this_row[i - 1] + 1),
                             last_row[i - 1] + (s1c == s2c ? 0 : 1));
    }
    std::swap(last_row, this_row);
  }
  return last_row[s1_length];
}

const AttributeSchema* Libraries::RetrieveAttributeSchema(Reporter* reporter,
                                                          const raw::Attribute& attribute) const {
  const auto& attribute_name = attribute.name;
  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    const auto& schema = iter->second;
    return &schema;
  }

  // Skip typo check?
  if (reporter == nullptr)
    return nullptr;

  // Match against all known attributes.
  for (const auto& name_and_schema : attribute_schemas_) {
    auto edit_distance = EditDistance(name_and_schema.first, attribute_name);
    if (0 < edit_distance && edit_distance < 2) {
      reporter->ReportWarning(WarnAttributeTypo, attribute.span(), attribute_name,
                              name_and_schema.first);
      return nullptr;
    }
  }

  return nullptr;
}

bool Dependencies::Register(const SourceSpan& span, std::string_view filename, Library* dep_library,
                            const std::unique_ptr<raw::Identifier>& maybe_alias) {
  refs_.push_back(std::make_unique<LibraryRef>(span, dep_library));
  auto ref = refs_.back().get();

  auto library_name = dep_library->name();
  if (!InsertByName(filename, library_name, ref)) {
    return false;
  }

  if (maybe_alias) {
    std::vector<std::string_view> alias_name = {maybe_alias->span().data()};
    if (!InsertByName(filename, alias_name, ref)) {
      return false;
    }
  }

  dependencies_aggregate_.insert(dep_library);

  return true;
}

bool Dependencies::InsertByName(std::string_view filename,
                                const std::vector<std::string_view>& name, LibraryRef* ref) {
  auto iter = dependencies_.find(std::string(filename));
  if (iter == dependencies_.end()) {
    dependencies_.emplace(filename, std::make_unique<ByName>());
  }

  iter = dependencies_.find(std::string(filename));
  assert(iter != dependencies_.end());

  auto insert = iter->second->emplace(name, ref);
  return insert.second;
}

bool Dependencies::Contains(std::string_view filename, const std::vector<std::string_view>& name) {
  auto iter1 = dependencies_.find(std::string(filename));
  if (iter1 == dependencies_.end()) {
    return false;
  }

  auto iter2 = iter1->second->find(name);
  return iter2 != iter1->second->end();
}

bool Dependencies::LookupAndUse(std::string_view filename,
                                const std::vector<std::string_view>& name, Library** out_library) {
  auto iter1 = dependencies_.find(std::string(filename));
  if (iter1 == dependencies_.end()) {
    return false;
  }

  auto iter2 = iter1->second->find(name);
  if (iter2 == iter1->second->end()) {
    return false;
  }

  auto ref = iter2->second;
  ref->used_ = true;
  *out_library = ref->library_;
  return true;
}

bool Dependencies::VerifyAllDependenciesWereUsed(const Library& for_library, Reporter* reporter) {
  auto checkpoint = reporter->Checkpoint();
  for (auto by_name_iter = dependencies_.begin(); by_name_iter != dependencies_.end();
       by_name_iter++) {
    const auto& by_name = *by_name_iter->second;
    for (const auto& name_to_ref : by_name) {
      const auto& ref = name_to_ref.second;
      if (ref->used_)
        continue;
      reporter->ReportError(ErrUnusedImport, ref->span_, for_library.name(), ref->library_->name(),
                            ref->library_->name());
    }
  }
  return checkpoint.NoNewErrors();
}

// Consuming the AST is primarily concerned with walking the tree and
// flattening the representation. The AST's declaration nodes are
// converted into the Library's foo_declaration structures. This means pulling
// a struct declaration inside a protocol out to the top level and
// so on.

std::string LibraryName(const Library* library, std::string_view separator) {
  if (library != nullptr) {
    return StringJoin(library->name(), separator);
  }
  return std::string();
}

bool Library::Fail(std::unique_ptr<Diagnostic> err) {
  assert(err && "should not report nullptr error");
  reporter_->ReportError(std::move(err));
  return false;
}

template <typename... Args>
bool Library::Fail(const ErrorDef<Args...>& err, const Args&... args) {
  reporter_->ReportError(err, args...);
  return false;
}

template <typename... Args>
bool Library::Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                   const Args&... args) {
  reporter_->ReportError(err, span, args...);
  return false;
}

void Library::ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                          const raw::AttributeList* attributes) {
  if (attributes == nullptr)
    return;
  for (const auto& attribute : attributes->attributes) {
    auto schema = all_libraries_->RetrieveAttributeSchema(reporter_, attribute);
    if (schema != nullptr) {
      schema->ValidatePlacement(reporter_, attribute, placement);
      schema->ValidateValue(reporter_, attribute);
    }
  }
}

void Library::ValidateAttributesConstraints(const Decl* decl,
                                            const raw::AttributeList* attributes) {
  if (attributes == nullptr)
    return;
  for (const auto& attribute : attributes->attributes) {
    auto schema = all_libraries_->RetrieveAttributeSchema(nullptr, attribute);
    if (schema != nullptr)
      schema->ValidateConstraint(reporter_, attribute, decl);
  }
}

SourceSpan Library::GeneratedSimpleName(const std::string& name) {
  return generated_source_file_.AddLine(name);
}

std::string Library::NextAnonymousName() {
  // TODO(FIDL-596): Improve anonymous name generation. We want to be
  // specific about how these names are generated once they appear in the
  // JSON IR, and are exposed to the backends.
  std::ostringstream data;
  data << "SomeLongAnonymousPrefix";
  data << anon_counter_++;

  return data.str();
}

std::optional<Name> Library::CompileCompoundIdentifier(
    const raw::CompoundIdentifier* compound_identifier) {
  const auto& components = compound_identifier->components;
  assert(components.size() >= 1);

  SourceSpan decl_name = components.back()->span();

  // First try resolving the identifier in the library.
  if (components.size() == 1) {
    return Name::CreateSourced(this, decl_name);
  }

  std::vector<std::string_view> library_name;
  for (auto iter = components.begin(); iter != components.end() - 1; ++iter) {
    library_name.push_back((*iter)->span().data());
  }

  auto filename = compound_identifier->span().source_file().filename();
  Library* dep_library = nullptr;
  if (dependencies_.LookupAndUse(filename, library_name, &dep_library)) {
    return Name::CreateSourced(dep_library, decl_name);
  }

  // If the identifier is not found in the library it might refer to a
  // declaration with a member (e.g. library.EnumX.val or BitsY.val).
  SourceSpan member_name = decl_name;
  SourceSpan member_decl_name = components.rbegin()[1]->span();

  if (components.size() == 2) {
    return Name::CreateSourced(this, member_decl_name, std::string(member_name.data()));
  }

  std::vector<std::string_view> member_library_name(library_name);
  member_library_name.pop_back();

  Library* member_dep_library = nullptr;
  if (dependencies_.LookupAndUse(filename, member_library_name, &member_dep_library)) {
    return Name::CreateSourced(member_dep_library, member_decl_name,
                               std::string(member_name.data()));
  }

  Fail(ErrUnknownDependentLibrary, components[0]->span(), library_name, member_library_name);
  return std::nullopt;
}

namespace {

template <typename T>
void StoreDecl(Decl* decl_ptr, std::vector<std::unique_ptr<T>>* declarations) {
  std::unique_ptr<T> t_decl;
  t_decl.reset(static_cast<T*>(decl_ptr));
  declarations->push_back(std::move(t_decl));
}

}  // namespace

bool Library::RegisterDecl(std::unique_ptr<Decl> decl) {
  assert(decl);

  auto decl_ptr = decl.release();
  auto kind = decl_ptr->kind;
  switch (kind) {
    case Decl::Kind::kBits:
      StoreDecl(decl_ptr, &bits_declarations_);
      break;
    case Decl::Kind::kConst:
      StoreDecl(decl_ptr, &const_declarations_);
      break;
    case Decl::Kind::kEnum:
      StoreDecl(decl_ptr, &enum_declarations_);
      break;
    case Decl::Kind::kProtocol:
      StoreDecl(decl_ptr, &protocol_declarations_);
      break;
    case Decl::Kind::kResource:
      StoreDecl(decl_ptr, &resource_declarations_);
      break;
    case Decl::Kind::kService:
      StoreDecl(decl_ptr, &service_declarations_);
      break;
    case Decl::Kind::kStruct:
      StoreDecl(decl_ptr, &struct_declarations_);
      break;
    case Decl::Kind::kTable:
      StoreDecl(decl_ptr, &table_declarations_);
      break;
    case Decl::Kind::kTypeAlias:
      StoreDecl(decl_ptr, &type_alias_declarations_);
      break;
    case Decl::Kind::kUnion:
      StoreDecl(decl_ptr, &union_declarations_);
      break;
  }  // switch

  const Name& name = decl_ptr->name;
  {
    const auto it = declarations_.emplace(name, decl_ptr);
    if (!it.second) {
      const auto previous_name = it.first->second->name;
      assert(previous_name.span() && "declarations_ has a name with no span");
      return Fail(ErrNameCollision, name.span(), name, *previous_name.span());
    }
  }

  const auto canonical_decl_name = utils::canonicalize(name.decl_name());
  {
    const auto it = declarations_by_canonical_name_.emplace(canonical_decl_name, decl_ptr);
    if (!it.second) {
      const auto previous_name = it.first->second->name;
      assert(previous_name.span() && "declarations_by_canonical_name_ has a name with no span");
      return Fail(ErrNameCollisionCanonical, name.span(), name, previous_name,
                  *previous_name.span(), canonical_decl_name);
    }
  }

  if (name.span()) {
    if (dependencies_.Contains(name.span()->source_file().filename(), {name.span()->data()})) {
      return Fail(ErrDeclNameConflictsWithLibraryImport, name, name);
    }
    if (dependencies_.Contains(name.span()->source_file().filename(), {canonical_decl_name})) {
      return Fail(ErrDeclNameConflictsWithLibraryImportCanonical, name, name, canonical_decl_name);
    }
  }

  switch (kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kResource:
    case Decl::Kind::kService:
    case Decl::Kind::kStruct:
    case Decl::Kind::kTable:
    case Decl::Kind::kUnion:
    case Decl::Kind::kProtocol: {
      auto type_decl = static_cast<TypeDecl*>(decl_ptr);
      auto type_template =
          std::make_unique<TypeDeclTypeTemplate>(name, typespace_, reporter_, this, type_decl);
      typespace_->AddTemplate(std::move(type_template));
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<TypeAlias*>(decl_ptr);
      auto type_alias_template = std::make_unique<TypeAliasTypeTemplate>(
          name, typespace_, reporter_, this, type_alias_decl);
      typespace_->AddTemplate(std::move(type_alias_template));
      break;
    }
    case Decl::Kind::kConst:
      break;
  }  // switch
  return true;
}

ConsumeStep Library::StartConsumeStep() { return ConsumeStep(this); }
CompileStep Library::StartCompileStep() { return CompileStep(this); }
VerifyAttributesStep Library::StartVerifyAttributesStep() { return VerifyAttributesStep(this); }

bool Library::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant,
                              std::unique_ptr<Constant>* out_constant) {
  switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
      auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
      auto name = CompileCompoundIdentifier(identifier->identifier.get());
      if (!name)
        return false;
      *out_constant =
          std::make_unique<IdentifierConstant>(std::move(name.value()), identifier->span());
      break;
    }
    case raw::Constant::Kind::kLiteral: {
      auto literal = static_cast<raw::LiteralConstant*>(raw_constant.get());
      *out_constant = std::make_unique<LiteralConstant>(std::move(literal->literal));
      break;
    }
    case raw::Constant::Kind::kBinaryOperator: {
      auto binary_operator_constant = static_cast<raw::BinaryOperatorConstant*>(raw_constant.get());
      BinaryOperatorConstant::Operator op;
      switch (binary_operator_constant->op) {
        case raw::BinaryOperatorConstant::Operator::kOr:
          op = BinaryOperatorConstant::Operator::kOr;
          break;
      }
      std::unique_ptr<Constant> left_operand;
      if (!ConsumeConstant(std::move(binary_operator_constant->left_operand), &left_operand)) {
        return false;
      }
      std::unique_ptr<Constant> right_operand;
      if (!ConsumeConstant(std::move(binary_operator_constant->right_operand), &right_operand)) {
        return false;
      }
      *out_constant = std::make_unique<BinaryOperatorConstant>(
          std::move(left_operand), std::move(right_operand), op, binary_operator_constant->span());
      break;
    }
  }
  return true;
}

bool Library::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                     SourceSpan span,
                                     std::unique_ptr<TypeConstructor>* out_type_ctor) {
  auto name = CompileCompoundIdentifier(raw_type_ctor->identifier.get());
  if (!name)
    return false;

  std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
  if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
    if (!ConsumeTypeConstructor(std::move(raw_type_ctor->maybe_arg_type_ctor), span,
                                &maybe_arg_type_ctor))
      return false;
  }

  std::unique_ptr<Constant> maybe_size;
  if (raw_type_ctor->maybe_size != nullptr) {
    if (!ConsumeConstant(std::move(raw_type_ctor->maybe_size), &maybe_size))
      return false;
  }

  std::unique_ptr<Constant> handle_rights;
  if (raw_type_ctor->handle_rights != nullptr) {
    if (!ConsumeConstant(std::move(raw_type_ctor->handle_rights), &handle_rights))
      return false;
  }

  // Only one of these should be set, either handle_subtype for "old",
  // or handle_subtype_identifier for "new". (Neither set is OK too for
  // an untyped handle.)
  std::optional<Name> handle_subtype_identifier;
  assert(!(raw_type_ctor->handle_subtype && raw_type_ctor->handle_subtype_identifier));
  if (raw_type_ctor->handle_subtype_identifier) {
    handle_subtype_identifier =
        Name::CreateSourced(this, raw_type_ctor->handle_subtype_identifier->span());
  }

  *out_type_ctor = std::make_unique<TypeConstructor>(
      std::move(name.value()), std::move(maybe_arg_type_ctor), raw_type_ctor->handle_subtype,
      std::move(handle_subtype_identifier), std::move(handle_rights), std::move(maybe_size),
      raw_type_ctor->nullability);
  return true;
}

void Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
  if (using_directive->maybe_type_ctor) {
    ConsumeTypeAlias(std::move(using_directive));
    return;
  }

  if (using_directive->attributes && using_directive->attributes->attributes.size() != 0) {
    Fail(ErrAttributesNotAllowedOnLibraryImport, using_directive->span(),
         *(using_directive->attributes));
    return;
  }

  std::vector<std::string_view> library_name;
  for (const auto& component : using_directive->using_path->components) {
    library_name.push_back(component->span().data());
  }

  Library* dep_library = nullptr;
  if (!all_libraries_->Lookup(library_name, &dep_library)) {
    Fail(ErrUnknownLibrary, using_directive->using_path->components[0]->span(), library_name);
    return;
  }

  auto filename = using_directive->span().source_file().filename();
  if (!dependencies_.Register(using_directive->span(), filename, dep_library,
                              using_directive->maybe_alias)) {
    Fail(ErrDuplicateLibraryImport, library_name);
    return;
  }

  // Import declarations, and type aliases of dependent library.
  const auto& declarations = dep_library->declarations_;
  declarations_.insert(declarations.begin(), declarations.end());
}

bool Library::ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive) {
  assert(using_directive->maybe_type_ctor);

  auto span = using_directive->using_path->components[0]->span();
  auto alias_name = Name::CreateSourced(this, span);
  std::unique_ptr<TypeConstructor> partial_type_ctor_;
  if (!ConsumeTypeConstructor(std::move(using_directive->maybe_type_ctor), span,
                              &partial_type_ctor_))
    return false;
  return RegisterDecl(std::make_unique<TypeAlias>(std::move(using_directive->attributes),
                                                  std::move(alias_name),
                                                  std::move(partial_type_ctor_)));
}

void Library::ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
  std::vector<Bits::Member> members;
  for (auto& member : bits_declaration->members) {
    auto span = member->identifier->span();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return;
    members.emplace_back(span, std::move(value), std::move(member->attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the bits name qualifier such as
    // <name_of_bits>.<name_of_member>.
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  if (bits_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructor(std::move(bits_declaration->maybe_type_ctor),
                                bits_declaration->span(), &type_ctor))
      return;
  } else {
    type_ctor = TypeConstructor::CreateSizeType();
  }

  RegisterDecl(std::make_unique<Bits>(
      std::move(bits_declaration->attributes),
      Name::CreateSourced(this, bits_declaration->identifier->span()), std::move(type_ctor),
      std::move(members), bits_declaration->strictness));
}

void Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
  auto attributes = std::move(const_declaration->attributes);
  auto span = const_declaration->identifier->span();
  auto name = Name::CreateSourced(this, span);
  std::unique_ptr<TypeConstructor> type_ctor;
  if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), span, &type_ctor))
    return;

  std::unique_ptr<Constant> constant;
  if (!ConsumeConstant(std::move(const_declaration->constant), &constant))
    return;

  RegisterDecl(std::make_unique<Const>(std::move(attributes), std::move(name), std::move(type_ctor),
                                       std::move(constant)));
}

void Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
  std::vector<Enum::Member> members;
  for (auto& member : enum_declaration->members) {
    auto span = member->identifier->span();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return;
    members.emplace_back(span, std::move(value), std::move(member->attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the enum name qualifier such as
    // <name_of_enum>.<name_of_member>.
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  if (enum_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructor(std::move(enum_declaration->maybe_type_ctor),
                                enum_declaration->span(), &type_ctor))
      return;
  } else {
    type_ctor = TypeConstructor::CreateSizeType();
  }

  RegisterDecl(std::make_unique<Enum>(
      std::move(enum_declaration->attributes),
      Name::CreateSourced(this, enum_declaration->identifier->span()), std::move(type_ctor),
      std::move(members), enum_declaration->strictness));
}

bool Library::CreateMethodResult(const Name& protocol_name, SourceSpan response_span,
                                 raw::ProtocolMethod* method, Struct* in_response,
                                 Struct** out_response) {
  // Compile the error type.
  auto error_span = method->maybe_error_ctor->span();
  std::unique_ptr<TypeConstructor> error_type_ctor;
  if (!ConsumeTypeConstructor(std::move(method->maybe_error_ctor), error_span, &error_type_ctor))
    return false;

  // Make the Result union containing the response struct and the
  // error type.
  SourceSpan method_name_span = method->identifier->span();

  // TODO(fxbug.dev/8027): Join spans of response and error constructor for `result_name`.
  auto result_name = Name::CreateDerived(
      this, response_span,
      StringJoin({protocol_name.decl_name(), method_name_span.data(), "Result"}, "_"));

  raw::SourceElement sourceElement = raw::SourceElement(fidl::Token(), fidl::Token());
  Union::Member response_member{
      std::make_unique<raw::Ordinal64>(sourceElement, 1),  // success case explicitly has ordinal 1
      IdentifierTypeForDecl(in_response, types::Nullability::kNonnullable),
      GeneratedSimpleName("response"), nullptr};
  Union::Member error_member{
      std::make_unique<raw::Ordinal64>(sourceElement, 2),  // error case explicitly has ordinal 2
      std::move(error_type_ctor), GeneratedSimpleName("err"), nullptr};
  std::vector<Union::Member> result_members;
  result_members.push_back(std::move(response_member));
  result_members.push_back(std::move(error_member));
  std::vector<raw::Attribute> result_attributes;
  result_attributes.emplace_back(*method, "Result", "");
  auto result_attributelist =
      std::make_unique<raw::AttributeList>(*method, std::move(result_attributes));
  // There is no syntax for indicating the resourceness of a method result type,
  // so we conservatively assume all such types are resources.
  const auto resourceness = types::Resourceness::kResource;
  auto union_decl =
      std::make_unique<Union>(std::move(result_attributelist), std::move(result_name),
                              std::move(result_members), types::Strictness::kStrict, resourceness);
  auto result_decl = union_decl.get();
  if (!RegisterDecl(std::move(union_decl)))
    return false;

  // Make a new response struct for the method containing just the
  // result union.
  std::vector<Struct::Member> response_members;
  response_members.push_back(
      Struct::Member(IdentifierTypeForDecl(result_decl, types::Nullability::kNonnullable),
                     GeneratedSimpleName("result"), nullptr, nullptr));

  auto struct_decl = std::make_unique<Struct>(
      nullptr /* attributes */, Name::CreateDerived(this, response_span, NextAnonymousName()),
      std::move(response_members), resourceness, true /* is_request_or_response */);
  auto struct_decl_ptr = struct_decl.get();
  if (!RegisterDecl(std::move(struct_decl)))
    return false;
  *out_response = struct_decl_ptr;
  return true;
}

void Library::ConsumeProtocolDeclaration(
    std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration) {
  auto attributes = std::move(protocol_declaration->attributes);
  auto name = Name::CreateSourced(this, protocol_declaration->identifier->span());

  std::set<Name> composed_protocols;
  for (auto& composed_protocol : protocol_declaration->composed_protocols) {
    auto& protocol_name = composed_protocol->protocol_name;
    auto composed_protocol_name = CompileCompoundIdentifier(protocol_name.get());
    if (!composed_protocol_name)
      return;
    if (!composed_protocols.insert(std::move(composed_protocol_name.value())).second) {
      Fail(ErrProtocolComposedMultipleTimes, composed_protocol_name->span());
      return;
    }
  }

  std::vector<Protocol::Method> methods;
  for (auto& method : protocol_declaration->methods) {
    auto selector_name =
        fidl::ordinals::GetSelector(method->attributes.get(), method->identifier->span());
    auto generated_ordinal64 = std::make_unique<raw::Ordinal64>(
        method_hasher_(library_name_, name.decl_name(), selector_name, *method->identifier));
    auto attributes = std::move(method->attributes);
    SourceSpan method_name = method->identifier->span();

    Struct* maybe_request = nullptr;
    if (method->maybe_request != nullptr) {
      auto request_span = method->maybe_request->span();
      auto request_name = Name::CreateDerived(this, request_span, NextAnonymousName());
      if (!ConsumeParameterList(std::move(request_name), std::move(method->maybe_request), true,
                                &maybe_request))
        return;
    }

    Struct* maybe_response = nullptr;
    if (method->maybe_response != nullptr) {
      const bool has_error = (method->maybe_error_ctor != nullptr);

      SourceSpan response_span = method->maybe_response->span();
      Name response_name = Name::CreateDerived(
          this, response_span,
          has_error ? StringJoin({name.decl_name(), method_name.data(), "Response"}, "_")
                    : NextAnonymousName());
      if (!ConsumeParameterList(std::move(response_name), std::move(method->maybe_response),
                                !has_error, &maybe_response))
        return;

      if (has_error) {
        if (!CreateMethodResult(name, response_span, method.get(), maybe_response, &maybe_response))
          return;
      }
    }

    assert(maybe_request != nullptr || maybe_response != nullptr);
    methods.emplace_back(std::move(attributes), std::move(generated_ordinal64),
                         std::move(method_name), std::move(maybe_request),
                         std::move(maybe_response));
  }

  RegisterDecl(std::make_unique<Protocol>(std::move(attributes), std::move(name),
                                          std::move(composed_protocols), std::move(methods)));
}

bool Library::ConsumeResourceDeclaration(
    std::unique_ptr<raw::ResourceDeclaration> resource_declaration) {
  std::vector<Resource::Property> properties;
  for (auto& property : resource_declaration->properties) {
    std::unique_ptr<TypeConstructor> type_ctor;
    auto span = property->identifier->span();
    if (!ConsumeTypeConstructor(std::move(property->type_ctor), span, &type_ctor))
      return false;
    auto attributes = std::move(property->attributes);
    properties.emplace_back(std::move(type_ctor), property->identifier->span(),
                            std::move(attributes));
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  if (resource_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructor(std::move(resource_declaration->maybe_type_ctor),
                                resource_declaration->span(), &type_ctor))
      return false;
  } else {
    type_ctor = TypeConstructor::CreateSizeType();
  }

  return RegisterDecl(std::make_unique<Resource>(
      std::move(resource_declaration->attributes),
      Name::CreateSourced(this, resource_declaration->identifier->span()), std::move(type_ctor),
      std::move(properties)));
}

std::unique_ptr<TypeConstructor> Library::IdentifierTypeForDecl(const Decl* decl,
                                                                types::Nullability nullability) {
  return std::make_unique<TypeConstructor>(
      decl->name, nullptr /* maybe_arg_type */, std::optional<types::HandleSubtype>(),
      std::optional<Name>() /* handle_subtype_identifier */, nullptr /* handle_rights */,
      nullptr /* maybe_size */, nullability);
}

bool Library::ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                                   bool is_request_or_response, Struct** out_struct_decl) {
  std::vector<Struct::Member> members;
  for (auto& parameter : parameter_list->parameter_list) {
    const SourceSpan name = parameter->identifier->span();
    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(parameter->type_ctor), name, &type_ctor))
      return false;
    ValidateAttributesPlacement(AttributeSchema::Placement::kStructMember,
                                parameter->attributes.get());
    members.emplace_back(std::move(type_ctor), name, nullptr /* maybe_default_value */,
                         std::move(parameter->attributes));
  }

  // There is no syntax for indicating the resourceness of a parameter list, so
  // we conservatively assume all parameter-list structs are resources.
  const auto resourceness = types::Resourceness::kResource;
  if (!RegisterDecl(std::make_unique<Struct>(nullptr /* attributes */, std::move(name),
                                             std::move(members), resourceness,
                                             is_request_or_response)))
    return false;
  *out_struct_decl = struct_declarations_.back().get();
  return true;
}

void Library::ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl) {
  auto attributes = std::move(service_decl->attributes);
  auto name = Name::CreateSourced(this, service_decl->identifier->span());

  std::vector<Service::Member> members;
  for (auto& member : service_decl->members) {
    std::unique_ptr<TypeConstructor> type_ctor;
    auto span = member->identifier->span();
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), span, &type_ctor))
      return;
    members.emplace_back(std::move(type_ctor), member->identifier->span(),
                         std::move(member->attributes));
  }

  RegisterDecl(
      std::make_unique<Service>(std::move(attributes), std::move(name), std::move(members)));
}

void Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
  auto attributes = std::move(struct_declaration->attributes);
  auto name = Name::CreateSourced(this, struct_declaration->identifier->span());

  std::vector<Struct::Member> members;
  for (auto& member : struct_declaration->members) {
    std::unique_ptr<TypeConstructor> type_ctor;
    auto span = member->identifier->span();
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), span, &type_ctor))
      return;
    std::unique_ptr<Constant> maybe_default_value;
    if (member->maybe_default_value != nullptr) {
      if (!ConsumeConstant(std::move(member->maybe_default_value), &maybe_default_value))
        return;
    }
    auto attributes = std::move(member->attributes);
    members.emplace_back(std::move(type_ctor), member->identifier->span(),
                         std::move(maybe_default_value), std::move(attributes));
  }

  RegisterDecl(std::make_unique<Struct>(std::move(attributes), std::move(name), std::move(members),
                                        struct_declaration->resourceness));
}

void Library::ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration) {
  auto attributes = std::move(table_declaration->attributes);
  auto name = Name::CreateSourced(this, table_declaration->identifier->span());

  std::vector<Table::Member> members;
  for (auto& member : table_declaration->members) {
    auto ordinal_literal = std::move(member->ordinal);

    if (member->maybe_used) {
      std::unique_ptr<TypeConstructor> type_ctor;
      if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), member->span(),
                                  &type_ctor))
        return;
      std::unique_ptr<Constant> maybe_default_value;
      if (member->maybe_used->maybe_default_value) {
        // TODO(FIDL-609): Support defaults on tables.
        const auto default_value = member->maybe_used->maybe_default_value.get();
        reporter_->ReportError(ErrDefaultsOnTablesNotSupported, default_value->span());
      }
      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        Fail(ErrNullableTableMember, member->span());
        return;
      }
      auto attributes = std::move(member->maybe_used->attributes);
      members.emplace_back(std::move(ordinal_literal), std::move(type_ctor),
                           member->maybe_used->identifier->span(), std::move(maybe_default_value),
                           std::move(attributes));
    } else {
      members.emplace_back(std::move(ordinal_literal), member->span());
    }
  }

  RegisterDecl(std::make_unique<Table>(std::move(attributes), std::move(name), std::move(members),
                                       table_declaration->strictness,
                                       table_declaration->resourceness));
}

void Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
  auto name = Name::CreateSourced(this, union_declaration->identifier->span());

  assert(!union_declaration->members.empty() && "unions must have at least one member");
  auto union_name =
      std::pair<std::string, std::string_view>(LibraryName(this, "."), name.decl_name());
  std::vector<Union::Member> members;
  for (auto& member : union_declaration->members) {
    auto explicit_ordinal = std::move(member->ordinal);

    if (member->maybe_used) {
      auto span = member->maybe_used->identifier->span();
      std::unique_ptr<TypeConstructor> type_ctor;
      if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), span, &type_ctor))
        return;
      if (member->maybe_used->maybe_default_value) {
        const auto default_value = member->maybe_used->maybe_default_value.get();
        reporter_->ReportError(ErrDefaultsOnUnionsNotSupported, default_value->span());
      }
      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        Fail(ErrNullableUnionMember, member->span());
        return;
      }

      members.emplace_back(std::move(explicit_ordinal), std::move(type_ctor), span,
                           std::move(member->maybe_used->attributes));
    } else {
      members.emplace_back(std::move(explicit_ordinal), member->span());
    }
  }

  RegisterDecl(std::make_unique<Union>(std::move(union_declaration->attributes), std::move(name),
                                       std::move(members), union_declaration->strictness,
                                       union_declaration->resourceness));
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
  if (file->attributes) {
    ValidateAttributesPlacement(AttributeSchema::Placement::kLibrary, file->attributes.get());
    if (!attributes_) {
      attributes_ = std::move(file->attributes);
    } else {
      AttributesBuilder attributes_builder(reporter_, std::move(attributes_->attributes));
      for (auto& attribute : file->attributes->attributes) {
        if (!attributes_builder.Insert(std::move(attribute)))
          return false;
      }
      attributes_ = std::make_unique<raw::AttributeList>(
          raw::SourceElement(file->attributes->start_, file->attributes->end_),
          attributes_builder.Done());
    }
  }

  // All fidl files in a library should agree on the library name.
  std::vector<std::string_view> new_name;
  for (const auto& part : file->library_name->components) {
    new_name.push_back(part->span().data());
  }
  if (!library_name_.empty()) {
    if (new_name != library_name_) {
      return Fail(ErrFilesDisagreeOnLibraryName, file->library_name->components[0]->span());
    }
  } else {
    library_name_ = new_name;
  }

  auto step = StartConsumeStep();

  auto using_list = std::move(file->using_list);
  for (auto& using_directive : using_list) {
    step.ForUsing(std::move(using_directive));
  }

  auto bits_declaration_list = std::move(file->bits_declaration_list);
  for (auto& bits_declaration : bits_declaration_list) {
    step.ForBitsDeclaration(std::move(bits_declaration));
  }

  auto const_declaration_list = std::move(file->const_declaration_list);
  for (auto& const_declaration : const_declaration_list) {
    step.ForConstDeclaration(std::move(const_declaration));
  }

  auto enum_declaration_list = std::move(file->enum_declaration_list);
  for (auto& enum_declaration : enum_declaration_list) {
    step.ForEnumDeclaration(std::move(enum_declaration));
  }

  auto protocol_declaration_list = std::move(file->protocol_declaration_list);
  for (auto& protocol_declaration : protocol_declaration_list) {
    step.ForProtocolDeclaration(std::move(protocol_declaration));
  }

  auto resource_declaration_list = std::move(file->resource_declaration_list);
  for (auto& resource_declaration : resource_declaration_list) {
    step.ForResourceDeclaration(std::move(resource_declaration));
  }

  auto service_declaration_list = std::move(file->service_declaration_list);
  for (auto& service_declaration : service_declaration_list) {
    step.ForServiceDeclaration(std::move(service_declaration));
  }

  auto struct_declaration_list = std::move(file->struct_declaration_list);
  for (auto& struct_declaration : struct_declaration_list) {
    step.ForStructDeclaration(std::move(struct_declaration));
  }

  auto table_declaration_list = std::move(file->table_declaration_list);
  for (auto& table_declaration : table_declaration_list) {
    step.ForTableDeclaration(std::move(table_declaration));
  }

  auto union_declaration_list = std::move(file->union_declaration_list);
  for (auto& union_declaration : union_declaration_list) {
    step.ForUnionDeclaration(std::move(union_declaration));
  }

  return step.Done();
}

bool Library::ResolveOrOperatorConstant(Constant* constant, const Type* type,
                                        const ConstantValue& left_operand,
                                        const ConstantValue& right_operand) {
  assert(left_operand.kind == right_operand.kind &&
         "left and right operands of or operator must be of the same kind");
  type = TypeResolve(type);
  if (type == nullptr)
    return false;
  if (type->kind != Type::Kind::kPrimitive) {
    return Fail(ErrOrOperatorOnNonPrimitiveValue);
  }
  std::unique_ptr<ConstantValue> left_operand_u64;
  std::unique_ptr<ConstantValue> right_operand_u64;
  if (!left_operand.Convert(ConstantValue::Kind::kUint64, &left_operand_u64))
    return false;
  if (!right_operand.Convert(ConstantValue::Kind::kUint64, &right_operand_u64))
    return false;
  NumericConstantValue<uint64_t> result =
      *static_cast<NumericConstantValue<uint64_t>*>(left_operand_u64.get()) |
      *static_cast<NumericConstantValue<uint64_t>*>(right_operand_u64.get());
  std::unique_ptr<ConstantValue> converted_result;
  if (!result.Convert(ConstantValuePrimitiveKind(static_cast<const PrimitiveType*>(type)->subtype),
                      &converted_result))
    return false;
  constant->ResolveTo(std::move(converted_result));
  return true;
}

bool Library::ResolveConstant(Constant* constant, const Type* type) {
  assert(constant != nullptr);

  // Prevent re-entry.
  if (constant->compiled)
    return constant->IsResolved();
  constant->compiled = true;

  switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
      auto identifier_constant = static_cast<IdentifierConstant*>(constant);
      return ResolveIdentifierConstant(identifier_constant, type);
    }
    case Constant::Kind::kLiteral: {
      auto literal_constant = static_cast<LiteralConstant*>(constant);
      return ResolveLiteralConstant(literal_constant, type);
    }
    case Constant::Kind::kSynthesized: {
      assert(false && "Compiler bug: synthesized constant does not have a resolved value!");
      break;
    }
    case Constant::Kind::kBinaryOperator: {
      auto binary_operator_constant = static_cast<BinaryOperatorConstant*>(constant);
      if (!ResolveConstant(binary_operator_constant->left_operand.get(), type)) {
        return false;
      }
      if (!ResolveConstant(binary_operator_constant->right_operand.get(), type)) {
        return false;
      }
      switch (binary_operator_constant->op) {
        case BinaryOperatorConstant::Operator::kOr: {
          return ResolveOrOperatorConstant(constant, type,
                                           binary_operator_constant->left_operand->Value(),
                                           binary_operator_constant->right_operand->Value());
        }
      }
      assert(false && "Compiler bug: unhandled binary operator");
      break;
    }
  }

  __builtin_unreachable();
}

ConstantValue::Kind Library::ConstantValuePrimitiveKind(
    const types::PrimitiveSubtype primitive_subtype) {
  switch (primitive_subtype) {
    case types::PrimitiveSubtype::kBool:
      return ConstantValue::Kind::kBool;
    case types::PrimitiveSubtype::kInt8:
      return ConstantValue::Kind::kInt8;
    case types::PrimitiveSubtype::kInt16:
      return ConstantValue::Kind::kInt16;
    case types::PrimitiveSubtype::kInt32:
      return ConstantValue::Kind::kInt32;
    case types::PrimitiveSubtype::kInt64:
      return ConstantValue::Kind::kInt64;
    case types::PrimitiveSubtype::kUint8:
      return ConstantValue::Kind::kUint8;
    case types::PrimitiveSubtype::kUint16:
      return ConstantValue::Kind::kUint16;
    case types::PrimitiveSubtype::kUint32:
      return ConstantValue::Kind::kUint32;
    case types::PrimitiveSubtype::kUint64:
      return ConstantValue::Kind::kUint64;
    case types::PrimitiveSubtype::kFloat32:
      return ConstantValue::Kind::kFloat32;
    case types::PrimitiveSubtype::kFloat64:
      return ConstantValue::Kind::kFloat64;
  }
  assert(false && "Compiler bug: unhandled primitive subtype");
}

bool Library::ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type) {
  assert(TypeCanBeConst(type) &&
         "Compiler bug: resolving identifier constant to non-const-able type!");

  auto decl = LookupDeclByName(identifier_constant->name.memberless_key());
  if (!decl)
    return false;

  if (!CompileDecl(decl)) {
    return false;
  }

  const TypeConstructor* const_type_ctor = nullptr;
  const ConstantValue* const_val = nullptr;
  switch (decl->kind) {
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<Const*>(decl);
      const_type_ctor = const_decl->type_ctor.get();
      const_val = &const_decl->value->Value();
      break;
    }
    case Decl::Kind::kEnum: {
      // If there is no member name, fallthrough to default.
      if (auto member_name = identifier_constant->name.member_name(); member_name) {
        auto enum_decl = static_cast<Enum*>(decl);
        const_type_ctor = enum_decl->subtype_ctor.get();
        for (auto& member : enum_decl->members) {
          if (member.name.data() == member_name) {
            const_val = &member.value->Value();
          }
        }
        if (!const_val) {
          return Fail(ErrUnknownEnumMember, identifier_constant->name.span(),
                      std::string_view(*member_name));
        }
        break;
      }
      [[fallthrough]];
    }
    case Decl::Kind::kBits: {
      // If there is no member name, fallthrough to default.
      if (auto member_name = identifier_constant->name.member_name(); member_name) {
        auto bits_decl = static_cast<Bits*>(decl);
        const_type_ctor = bits_decl->subtype_ctor.get();
        for (auto& member : bits_decl->members) {
          if (member.name.data() == member_name) {
            const_val = &member.value->Value();
          }
        }
        if (!const_val) {
          return Fail(ErrUnknownBitsMember, identifier_constant->name.span(),
                      std::string_view(*member_name));
        }
        break;
      }
      [[fallthrough]];
    }
    default: {
      return Fail(ErrExpectedValueButGotType, identifier_constant->name.span(),
                  identifier_constant);
    }
  }

  assert(const_val && "Compiler bug: did not set const_val");
  assert(const_type_ctor && "Compiler bug: did not set const_type_ctor");

  std::unique_ptr<ConstantValue> resolved_val;
  switch (type->kind) {
    case Type::Kind::kString: {
      if (!TypeIsConvertibleTo(const_type_ctor->type, type))
        goto fail_cannot_convert;

      if (!const_val->Convert(ConstantValue::Kind::kString, &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    case Type::Kind::kPrimitive: {
      auto primitive_type = static_cast<const PrimitiveType*>(type);
      if (!const_val->Convert(ConstantValuePrimitiveKind(primitive_type->subtype), &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      const PrimitiveType* primitive_type;
      switch (identifier_type->type_decl->kind) {
        case Decl::Kind::kEnum: {
          auto enum_decl = static_cast<const Enum*>(identifier_type->type_decl);
          assert(enum_decl->subtype_ctor->type->kind == Type::Kind::kPrimitive);
          primitive_type = static_cast<const PrimitiveType*>(enum_decl->subtype_ctor->type);
          break;
        }
        case Decl::Kind::kBits: {
          auto bits_decl = static_cast<const Bits*>(identifier_type->type_decl);
          assert(bits_decl->subtype_ctor->type->kind == Type::Kind::kPrimitive);
          primitive_type = static_cast<const PrimitiveType*>(bits_decl->subtype_ctor->type);
          break;
        }
        default: {
          assert(false && "Compiler bug: identifier not of const-able type.");
        }
      }

      auto fail_with_mismatched_type = [this, identifier_type](const Name& type_name) {
        return Fail(ErrMismatchedNameTypeAssignment, identifier_type->type_decl->name, type_name);
      };

      switch (decl->kind) {
        case Decl::Kind::kConst: {
          if (const_type_ctor->type->name != identifier_type->type_decl->name)
            return fail_with_mismatched_type(const_type_ctor->type->name);
          break;
        }
        case Decl::Kind::kBits:
        case Decl::Kind::kEnum: {
          if (decl->name != identifier_type->type_decl->name)
            return fail_with_mismatched_type(decl->name);
          break;
        }
        default: {
          assert(false && "Compiler bug: identifier not of const-able type.");
        }
      }

      if (!const_val->Convert(ConstantValuePrimitiveKind(primitive_type->subtype), &resolved_val))
        goto fail_cannot_convert;
      break;
    }
    default: {
      assert(false && "Compiler bug: identifier not of const-able type.");
    }
  }

  identifier_constant->ResolveTo(std::move(resolved_val));
  return true;

fail_cannot_convert:
  return Fail(ErrCannotConvertConstantToType, identifier_constant, const_type_ctor, type);
}

bool Library::ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type) {
  switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kString: {
      if (type->kind != Type::Kind::kString)
        goto return_fail;
      auto string_type = static_cast<const StringType*>(type);
      auto string_literal = static_cast<raw::StringLiteral*>(literal_constant->literal.get());
      auto string_data = string_literal->span().data();

      // TODO(pascallouis): because data() contains the raw content,
      // with the two " to identify strings, we need to take this
      // into account. We should expose the actual size of string
      // literals properly, and take into account escaping.
      uint64_t string_size = string_data.size() - 2;
      if (string_type->max_size->value < string_size) {
        return Fail(ErrStringConstantExceedsSizeBound, literal_constant->literal->span(),
                    literal_constant, string_size, type);
      }

      literal_constant->ResolveTo(
          std::make_unique<StringConstantValue>(string_literal->span().data()));
      return true;
    }
    case raw::Literal::Kind::kTrue: {
      if (type->kind != Type::Kind::kPrimitive)
        goto return_fail;
      if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
        goto return_fail;
      literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(true));
      return true;
    }
    case raw::Literal::Kind::kFalse: {
      if (type->kind != Type::Kind::kPrimitive)
        goto return_fail;
      if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
        goto return_fail;
      literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(false));
      return true;
    }
    case raw::Literal::Kind::kNumeric: {
      if (type->kind != Type::Kind::kPrimitive)
        goto return_fail;

      // These must be initialized out of line to allow for goto statement
      const raw::NumericLiteral* numeric_literal;
      const PrimitiveType* primitive_type;
      numeric_literal = static_cast<const raw::NumericLiteral*>(literal_constant->literal.get());
      primitive_type = static_cast<const PrimitiveType*>(type);
      switch (primitive_type->subtype) {
        case types::PrimitiveSubtype::kInt8: {
          int8_t value;
          if (!ParseNumericLiteral<int8_t>(numeric_literal, &value))
            goto return_fail;
          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int8_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kInt16: {
          int16_t value;
          if (!ParseNumericLiteral<int16_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int16_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kInt32: {
          int32_t value;
          if (!ParseNumericLiteral<int32_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int32_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kInt64: {
          int64_t value;
          if (!ParseNumericLiteral<int64_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int64_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kUint8: {
          uint8_t value;
          if (!ParseNumericLiteral<uint8_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint8_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kUint16: {
          uint16_t value;
          if (!ParseNumericLiteral<uint16_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint16_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kUint32: {
          uint32_t value;
          if (!ParseNumericLiteral<uint32_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint32_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kUint64: {
          uint64_t value;
          if (!ParseNumericLiteral<uint64_t>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint64_t>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kFloat32: {
          float value;
          if (!ParseNumericLiteral<float>(numeric_literal, &value))
            goto return_fail;
          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<float>>(value));
          return true;
        }
        case types::PrimitiveSubtype::kFloat64: {
          double value;
          if (!ParseNumericLiteral<double>(numeric_literal, &value))
            goto return_fail;

          literal_constant->ResolveTo(std::make_unique<NumericConstantValue<double>>(value));
          return true;
        }
        default:
          goto return_fail;
      }

    return_fail:
      return Fail(ErrConstantCannotBeInterpretedAsType, literal_constant->literal->span(),
                  literal_constant, type);
    }
  }
}

const Type* Library::TypeResolve(const Type* type) {
  if (type->kind != Type::Kind::kIdentifier) {
    return type;
  }
  auto identifier_type = static_cast<const IdentifierType*>(type);
  Decl* decl = LookupDeclByName(identifier_type->name);
  if (!decl) {
    Fail(ErrCouldNotResolveIdentifierToType);
    return nullptr;
  }
  if (!CompileDecl(decl))
    return nullptr;
  switch (decl->kind) {
    case Decl::Kind::kBits:
      return static_cast<const Bits*>(decl)->subtype_ctor->type;
    case Decl::Kind::kEnum:
      return static_cast<const Enum*>(decl)->subtype_ctor->type;
    default:
      return type;
  }
}

bool Library::TypeCanBeConst(const Type* type) {
  switch (type->kind) {
    case flat::Type::Kind::kString:
      return type->nullability != types::Nullability::kNullable;
    case flat::Type::Kind::kPrimitive:
      return true;
    case flat::Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      switch (identifier_type->type_decl->kind) {
        case Decl::Kind::kEnum:
        case Decl::Kind::kBits:
          return true;
        default:
          return false;
      }
    }
    default:
      return false;
  }  // switch
}

bool Library::TypeIsConvertibleTo(const Type* from_type, const Type* to_type) {
  switch (to_type->kind) {
    case flat::Type::Kind::kString: {
      if (from_type->kind != flat::Type::Kind::kString)
        return false;

      auto from_string_type = static_cast<const flat::StringType*>(from_type);
      auto to_string_type = static_cast<const flat::StringType*>(to_type);

      if (to_string_type->nullability == types::Nullability::kNonnullable &&
          from_string_type->nullability != types::Nullability::kNonnullable)
        return false;

      if (to_string_type->max_size->value < from_string_type->max_size->value)
        return false;

      return true;
    }
    case flat::Type::Kind::kPrimitive: {
      if (from_type->kind != flat::Type::Kind::kPrimitive) {
        return false;
      }

      auto from_primitive_type = static_cast<const flat::PrimitiveType*>(from_type);
      auto to_primitive_type = static_cast<const flat::PrimitiveType*>(to_type);

      switch (to_primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
          return from_primitive_type->subtype == types::PrimitiveSubtype::kBool;
        default:
          // TODO(pascallouis): be more precise about convertibility, e.g. it
          // should not be allowed to convert a float to an int.
          return from_primitive_type->subtype != types::PrimitiveSubtype::kBool;
      }
    }
    default:
      return false;
  }  // switch
}

// Library resolution is concerned with resolving identifiers to their
// declarations, and with computing type sizes and alignments.

Decl* Library::LookupDeclByName(Name::Key name) const {
  auto iter = declarations_.find(name);
  if (iter == declarations_.end()) {
    return nullptr;
  }
  return iter->second;
}

template <typename NumericType>
bool Library::ParseNumericLiteral(const raw::NumericLiteral* literal,
                                  NumericType* out_value) const {
  assert(literal != nullptr);
  assert(out_value != nullptr);

  auto data = literal->span().data();
  std::string string_data(data.data(), data.data() + data.size());
  auto result = utils::ParseNumeric(string_data, out_value);
  return result == utils::ParseNumericResult::kSuccess;
}

bool Library::AddConstantDependencies(const Constant* constant, std::set<Decl*>* out_edges) {
  switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
      auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
      auto decl = LookupDeclByName(identifier->name.memberless_key());
      if (decl == nullptr) {
        return Fail(ErrFailedConstantLookup, identifier->name, identifier->name);
      }
      out_edges->insert(decl);
      break;
    }
    case Constant::Kind::kLiteral:
    case Constant::Kind::kSynthesized: {
      // Literal and synthesized constants have no dependencies on other declarations.
      break;
    }
    case Constant::Kind::kBinaryOperator: {
      auto op = static_cast<const flat::BinaryOperatorConstant*>(constant);
      return AddConstantDependencies(op->left_operand.get(), out_edges) &&
             AddConstantDependencies(op->right_operand.get(), out_edges);
    }
  }
  return true;
}

// Calculating declaration dependencies is largely serving the C/C++ family of languages bindings.
// For instance, the declaration of a struct member type must be defined before the containing
// struct if that member is stored inline.
// Given the FIDL declarations:
//
//     struct D2 { D1 d; }
//     struct D1 { int32 x; }
//
// We must first declare D1, followed by D2 when emitting C code.
//
// Below, an edge from D1 to D2 means that we must see the declaration of of D1 before
// the declaration of D2, i.e. the calculated set of |out_edges| represents all the declarations
// that |decl| depends on.
//
// Notes:
// - Nullable structs do not require dependency edges since they are boxed via a
// pointer indirection, and their content placed out-of-line.
bool Library::DeclDependencies(Decl* decl, std::set<Decl*>* out_edges) {
  std::set<Decl*> edges;
  auto maybe_add_decl = [this, &edges](const TypeConstructor* type_ctor) {
    for (;;) {
      const auto& name = type_ctor->name;
      if (name.decl_name() == "request") {
        return;
      } else if (type_ctor->maybe_arg_type_ctor) {
        type_ctor = type_ctor->maybe_arg_type_ctor.get();
      } else if (type_ctor->nullability == types::Nullability::kNullable) {
        return;
      } else {
        if (auto decl = LookupDeclByName(name); decl && decl->kind != Decl::Kind::kProtocol) {
          edges.insert(decl);
        }
        return;
      }
    }
  };
  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<const Bits*>(decl);
      maybe_add_decl(bits_decl->subtype_ctor.get());
      for (const auto& member : bits_decl->members) {
        if (!AddConstantDependencies(member.value.get(), &edges)) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      maybe_add_decl(const_decl->type_ctor.get());
      if (!AddConstantDependencies(const_decl->value.get(), &edges)) {
        return false;
      }
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<const Enum*>(decl);
      maybe_add_decl(enum_decl->subtype_ctor.get());
      for (const auto& member : enum_decl->members) {
        if (!AddConstantDependencies(member.value.get(), &edges)) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_decl = static_cast<const Protocol*>(decl);
      for (const auto& composed_protocol : protocol_decl->composed_protocols) {
        if (auto type_decl = LookupDeclByName(composed_protocol); type_decl) {
          edges.insert(type_decl);
        }
      }
      for (const auto& method : protocol_decl->methods) {
        if (method.maybe_request != nullptr) {
          edges.insert(method.maybe_request);
        }
        if (method.maybe_response != nullptr) {
          edges.insert(method.maybe_response);
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_decl = static_cast<const Resource*>(decl);
      maybe_add_decl(resource_decl->subtype_ctor.get());
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<const Service*>(decl);
      for (const auto& member : service_decl->members) {
        maybe_add_decl(member.type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      for (const auto& member : struct_decl->members) {
        maybe_add_decl(member.type_ctor.get());
        if (member.maybe_default_value) {
          if (!AddConstantDependencies(member.maybe_default_value.get(), &edges)) {
            return false;
          }
        }
      }
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      for (const auto& member : table_decl->members) {
        if (!member.maybe_used)
          continue;
        maybe_add_decl(member.maybe_used->type_ctor.get());
        if (member.maybe_used->maybe_default_value) {
          if (!AddConstantDependencies(member.maybe_used->maybe_default_value.get(), &edges)) {
            return false;
          }
        }
      }
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      for (const auto& member : union_decl->members) {
        if (!member.maybe_used)
          continue;
        maybe_add_decl(member.maybe_used->type_ctor.get());
      }
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<const TypeAlias*>(decl);
      maybe_add_decl(type_alias_decl->partial_type_ctor.get());
    }
  }  // switch
  *out_edges = std::move(edges);
  return true;
}

namespace {
// Declaration comparator.
//
// (1) To compare two Decl's in the same library, it suffices to compare the
//     unqualified names of the Decl's. (This is faster.)
//
// (2) To compare two Decl's across libraries, we rely on the fully qualified
//     names of the Decl's. (This is slower.)
struct CmpDeclInLibrary {
  bool operator()(const Decl* a, const Decl* b) const {
    assert(a->name != b->name || a == b);
    const Library* a_library = a->name.library();
    const Library* b_library = b->name.library();
    if (a_library != b_library) {
      return NameFlatName(a->name) < NameFlatName(b->name);
    } else {
      return a->name.decl_name() < b->name.decl_name();
    }
  }
};
}  // namespace

bool Library::SortDeclarations() {
  // |degree| is the number of undeclared dependencies for each decl.
  std::map<Decl*, uint32_t, CmpDeclInLibrary> degrees;
  // |inverse_dependencies| records the decls that depend on each decl.
  std::map<Decl*, std::vector<Decl*>, CmpDeclInLibrary> inverse_dependencies;
  for (auto& name_and_decl : declarations_) {
    Decl* decl = name_and_decl.second;
    std::set<Decl*> deps;
    if (!DeclDependencies(decl, &deps))
      return false;
    degrees[decl] = static_cast<uint32_t>(deps.size());
    for (Decl* dep : deps) {
      inverse_dependencies[dep].push_back(decl);
    }
  }

  // Start with all decls that have no incoming edges.
  std::vector<Decl*> decls_without_deps;
  for (const auto& decl_and_degree : degrees) {
    if (decl_and_degree.second == 0u) {
      decls_without_deps.push_back(decl_and_degree.first);
    }
  }

  while (!decls_without_deps.empty()) {
    // Pull one out of the queue.
    auto decl = decls_without_deps.back();
    decls_without_deps.pop_back();
    assert(degrees[decl] == 0u);
    declaration_order_.push_back(decl);

    // Decrement the incoming degree of all the other decls it
    // points to.
    auto& inverse_deps = inverse_dependencies[decl];
    for (Decl* inverse_dep : inverse_deps) {
      uint32_t& degree = degrees[inverse_dep];
      assert(degree != 0u);
      degree -= 1;
      if (degree == 0u)
        decls_without_deps.push_back(inverse_dep);
    }
  }

  if (declaration_order_.size() != degrees.size()) {
    // We didn't visit all the edges! There was a cycle.
    return Fail(ErrIncludeCycle);
  }

  return true;
}

bool Library::CompileDecl(Decl* decl) {
  if (decl->compiled)
    return true;
  Compiling guard(decl);
  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<Bits*>(decl);
      if (!CompileBits(bits_decl))
        return false;
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<Const*>(decl);
      if (!CompileConst(const_decl))
        return false;
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<Enum*>(decl);
      if (!CompileEnum(enum_decl))
        return false;
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_decl = static_cast<Protocol*>(decl);
      if (!CompileProtocol(protocol_decl))
        return false;
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_decl = static_cast<Resource*>(decl);
      if (!CompileResource(resource_decl))
        return false;
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<Service*>(decl);
      if (!CompileService(service_decl))
        return false;
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<Struct*>(decl);
      if (!CompileStruct(struct_decl))
        return false;
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<Table*>(decl);
      if (!CompileTable(table_decl))
        return false;
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<Union*>(decl);
      if (!CompileUnion(union_decl))
        return false;
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<TypeAlias*>(decl);
      if (!CompileTypeAlias(type_alias_decl))
        return false;
      break;
    }
  }  // switch
  return true;
}

void Library::VerifyDeclAttributes(Decl* decl) {
  assert(decl->compiled && "verification must happen after compilation of decls");
  auto placement_ok = reporter_->Checkpoint();
  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_declaration = static_cast<Bits*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kBitsDecl,
                                  bits_declaration->attributes.get());
      for (const auto& member : bits_declaration->members) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kBitsMember,
                                    member.attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(bits_declaration, bits_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<Const*>(decl);
      // Attributes: for const declarations, we only check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kConstDecl,
                                  const_decl->attributes.get());
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_declaration = static_cast<Enum*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kEnumDecl,
                                  enum_declaration->attributes.get());
      for (const auto& member : enum_declaration->members) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kEnumMember,
                                    member.attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(enum_declaration, enum_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_declaration = static_cast<Protocol*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kProtocolDecl,
                                  protocol_declaration->attributes.get());
      for (const auto& method_with_info : protocol_declaration->all_methods) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kMethod,
                                    method_with_info.method->attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        for (const auto method_with_info : protocol_declaration->all_methods) {
          const auto& method = *method_with_info.method;
          if (method.maybe_request) {
            ValidateAttributesConstraints(method.maybe_request,
                                          protocol_declaration->attributes.get());
            ValidateAttributesConstraints(method.maybe_request, method.attributes.get());
          }
          if (method.maybe_response) {
            ValidateAttributesConstraints(method.maybe_response,
                                          protocol_declaration->attributes.get());
            ValidateAttributesConstraints(method.maybe_response, method.attributes.get());
          }
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_declaration = static_cast<Resource*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kResourceDecl,
                                  resource_declaration->attributes.get());
      for (const auto& property : resource_declaration->properties) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kResourceProperty,
                                    property.attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(resource_declaration, resource_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<Service*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kServiceDecl,
                                  service_decl->attributes.get());
      for (const auto& member : service_decl->members) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kServiceMember,
                                    member.attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(service_decl, service_decl->attributes.get());
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_declaration = static_cast<Struct*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kStructDecl,
                                  struct_declaration->attributes.get());
      for (const auto& member : struct_declaration->members) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kStructMember,
                                    member.attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(struct_declaration, struct_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kTable: {
      auto table_declaration = static_cast<Table*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kTableDecl,
                                  table_declaration->attributes.get());
      for (const auto& member : table_declaration->members) {
        if (!member.maybe_used)
          continue;
        ValidateAttributesPlacement(AttributeSchema::Placement::kTableMember,
                                    member.maybe_used->attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(table_declaration, table_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_declaration = static_cast<Union*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kUnionDecl,
                                  union_declaration->attributes.get());
      for (const auto& member : union_declaration->members) {
        if (!member.maybe_used)
          continue;
        ValidateAttributesPlacement(AttributeSchema::Placement::kUnionMember,
                                    member.maybe_used->attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(union_declaration, union_declaration->attributes.get());
      }
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_declaration = static_cast<TypeAlias*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kTypeAliasDecl,
                                  type_alias_declaration->attributes.get());
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(type_alias_declaration,
                                      type_alias_declaration->attributes.get());
      }
      break;
    }
  }  // switch
}

bool Library::CompileBits(Bits* bits_declaration) {
  if (!CompileTypeConstructor(bits_declaration->subtype_ctor.get()))
    return false;

  if (bits_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    return Fail(ErrBitsTypeMustBeUnsignedIntegralPrimitive, *bits_declaration,
                bits_declaration->subtype_ctor->type);
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(bits_declaration->subtype_ctor->type);
  switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kUint8: {
      uint8_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint8_t>(bits_declaration, &mask))
        return false;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint16: {
      uint16_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint16_t>(bits_declaration, &mask))
        return false;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint32: {
      uint32_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint32_t>(bits_declaration, &mask))
        return false;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kUint64: {
      uint64_t mask;
      if (!ValidateBitsMembersAndCalcMask<uint64_t>(bits_declaration, &mask))
        return false;
      bits_declaration->mask = mask;
      break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      return Fail(ErrBitsTypeMustBeUnsignedIntegralPrimitive, *bits_declaration,
                  bits_declaration->subtype_ctor->type);
  }

  {
    // In the line below, `nullptr` needs an explicit cast to the pointer type due to
    // C++ template mechanics.
    auto err = ValidateUnknownConstraints<const Bits::Member>(
        *bits_declaration, bits_declaration->strictness, nullptr);
    if (err) {
      return Fail(std::move(err));
    }
  }

  return true;
}

bool Library::CompileConst(Const* const_declaration) {
  if (!CompileTypeConstructor(const_declaration->type_ctor.get()))
    return false;
  const auto* const_type = const_declaration->type_ctor.get()->type;
  if (!TypeCanBeConst(const_type)) {
    return Fail(ErrInvalidConstantType, *const_declaration, const_type);
  }
  if (!ResolveConstant(const_declaration->value.get(), const_type))
    return Fail(ErrCannotResolveConstantValue, *const_declaration);

  return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
  if (!CompileTypeConstructor(enum_declaration->subtype_ctor.get()))
    return false;

  if (enum_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    return Fail(ErrEnumTypeMustBeIntegralPrimitive, *enum_declaration,
                enum_declaration->subtype_ctor->type);
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(enum_declaration->subtype_ctor->type);
  enum_declaration->type = primitive_type;
  switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kInt8: {
      int8_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<int8_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_signed = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kInt16: {
      int16_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<int16_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_signed = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kInt32: {
      int32_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<int32_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_signed = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kInt64: {
      int64_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<int64_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_signed = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kUint8: {
      uint8_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<uint8_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_unsigned = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kUint16: {
      uint16_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<uint16_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_unsigned = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kUint32: {
      uint32_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<uint32_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_unsigned = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kUint64: {
      uint64_t unknown_value;
      if (!ValidateEnumMembersAndCalcUnknownValue<uint64_t>(enum_declaration, &unknown_value))
        return false;
      enum_declaration->unknown_value_unsigned = unknown_value;
      break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      return Fail(ErrEnumTypeMustBeIntegralPrimitive, *enum_declaration,
                  enum_declaration->subtype_ctor->type);
  }

  return true;
}

bool HasSimpleLayout(const Decl* decl) { return decl->HasAttribute("ForDeprecatedCBindings"); }

bool Library::CompileResource(Resource* resource_declaration) {
  Scope<std::string_view> scope;
  if (!CompileTypeConstructor(resource_declaration->subtype_ctor.get()))
    return false;

  if (resource_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    return Fail(ErrEnumTypeMustBeIntegralPrimitive, *resource_declaration,
                resource_declaration->subtype_ctor->type);
  }

  for (auto& property : resource_declaration->properties) {
    auto name_result = scope.Insert(property.name.data(), property.name);
    if (!name_result.ok())
      return Fail(ErrDuplicateResourcePropertyName, property.name,
                  name_result.previous_occurrence());

    if (!CompileTypeConstructor(property.type_ctor.get()))
      return false;
  }
  return true;
}

bool Library::CompileProtocol(Protocol* protocol_declaration) {
  MethodScope method_scope;
  auto CheckScopes = [this, &protocol_declaration, &method_scope](const Protocol* protocol,
                                                                  auto Visitor) -> bool {
    for (const auto& name : protocol->composed_protocols) {
      auto decl = LookupDeclByName(name);
      // TODO(FIDL-603): Special handling here should not be required, we
      // should first rely on creating the types representing composed
      // protocols.
      if (!decl) {
        return Fail(ErrUnknownType, name, name);
      }
      if (decl->kind != Decl::Kind::kProtocol)
        return Fail(ErrComposingNonProtocol, name);
      auto composed_protocol = static_cast<const Protocol*>(decl);
      auto span = composed_protocol->name.span();
      assert(span);
      if (method_scope.protocols.Insert(composed_protocol, span.value()).ok()) {
        if (!Visitor(composed_protocol, Visitor))
          return false;
      } else {
        // Otherwise we have already seen this protocol in
        // the inheritance graph.
      }
    }
    for (const auto& method : protocol->methods) {
      const auto original_name = method.name.data();
      const auto canonical_name = utils::canonicalize(original_name);
      const auto name_result = method_scope.canonical_names.Insert(canonical_name, method.name);
      if (!name_result.ok()) {
        if (original_name == name_result.previous_occurrence().data()) {
          return Fail(ErrDuplicateMethodName, method.name, original_name,
                      name_result.previous_occurrence());
        }
        const auto previous_span = name_result.previous_occurrence();
        return Fail(ErrDuplicateMethodNameCanonical, method.name, original_name,
                    previous_span.data(), previous_span, canonical_name);
      }
      if (method.generated_ordinal64->value == 0)
        return Fail(ErrGeneratedZeroValueOrdinal, method.generated_ordinal64->span());
      auto ordinal_result =
          method_scope.ordinals.Insert(method.generated_ordinal64->value, method.name);
      if (!ordinal_result.ok()) {
        std::string replacement_method(
            fidl::ordinals::GetSelector(method.attributes.get(), method.name));
        replacement_method.push_back('_');
        return Fail(ErrDuplicateMethodOrdinal, method.generated_ordinal64->span(),
                    ordinal_result.previous_occurrence(), replacement_method);
      }

      // Add a pointer to this method to the protocol_declarations list.
      bool is_composed = protocol_declaration != protocol;
      protocol_declaration->all_methods.emplace_back(&method, is_composed);
    }
    return true;
  };
  if (!CheckScopes(protocol_declaration, CheckScopes))
    return false;

  for (auto& method : protocol_declaration->methods) {
    if (method.maybe_request) {
      if (!CompileDecl(method.maybe_request))
        return false;
    }
    if (method.maybe_response) {
      if (!CompileDecl(method.maybe_response))
        return false;
    }
  }

  return true;
}

bool Library::CompileService(Service* service_decl) {
  Scope<std::string> scope;
  for (const auto& member : service_decl->members) {
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == name_result.previous_occurrence().data()) {
        return Fail(ErrDuplicateServiceMemberName, member.name, original_name, previous_span);
      }
      return Fail(ErrDuplicateServiceMemberNameCanonical, member.name, original_name,
                  previous_span.data(), previous_span, canonical_name);
    }
    if (!CompileTypeConstructor(member.type_ctor.get()))
      return false;
    if (member.type_ctor->type->kind != Type::Kind::kIdentifier)
      return Fail(ErrNonProtocolServiceMember, member.name);
    auto member_identifier_type = static_cast<const IdentifierType*>(member.type_ctor->type);
    if (member_identifier_type->type_decl->kind != Decl::Kind::kProtocol)
      return Fail(ErrNonProtocolServiceMember, member.name);
    if (member.type_ctor->nullability != types::Nullability::kNonnullable)
      return Fail(ErrNullableServiceMember, member.name);
  }
  return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
  Scope<std::string> scope;
  const StructMember* first_resource_member = nullptr;
  for (const auto& member : struct_declaration->members) {
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      if (original_name == previous_span.data()) {
        return Fail(struct_declaration->is_request_or_response ? ErrDuplicateMethodParameterName
                                                               : ErrDuplicateStructMemberName,
                    member.name, original_name, previous_span);
      }
      return Fail(struct_declaration->is_request_or_response
                      ? ErrDuplicateMethodParameterNameCanonical
                      : ErrDuplicateStructMemberNameCanonical,
                  member.name, original_name, previous_span.data(), previous_span, canonical_name);
    }

    if (!CompileTypeConstructor(member.type_ctor.get()))
      return false;
    assert(!(struct_declaration->is_request_or_response && member.maybe_default_value) &&
           "method parameters cannot have default values");
    if (!first_resource_member && IsResourceType(member.type_ctor->type)) {
      first_resource_member = &member;
    }
    if (member.maybe_default_value) {
      const auto* default_value_type = member.type_ctor->type;
      if (!TypeCanBeConst(default_value_type)) {
        return Fail(ErrInvalidStructMemberType, *struct_declaration, NameIdentifier(member.name),
                    default_value_type);
      }
      if (!ResolveConstant(member.maybe_default_value.get(), default_value_type)) {
        return false;
      }
    }
  }

  if (first_resource_member && struct_declaration->resourceness == types::Resourceness::kValue) {
    return Fail(ErrResourceTypeInValueType, first_resource_member->name,
                first_resource_member->type_ctor->type, struct_declaration->name,
                first_resource_member->name.data(), struct_declaration->name);
  }

  return true;
}

bool Library::CompileTable(Table* table_declaration) {
  Scope<std::string> name_scope;
  Ordinal64Scope ordinal_scope;
  const Table::Member::Used* first_resource_member = nullptr;

  for (const auto& member : table_declaration->members) {
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      return Fail(ErrDuplicateTableFieldOrdinal, member.ordinal->span(),
                  ordinal_result.previous_occurrence());
    }
    if (member.maybe_used) {
      const auto& member_used = *member.maybe_used;
      const auto original_name = member_used.name.data();
      const auto canonical_name = utils::canonicalize(original_name);
      const auto name_result = name_scope.Insert(canonical_name, member_used.name);
      if (!name_result.ok()) {
        const auto previous_span = name_result.previous_occurrence();
        if (original_name == name_result.previous_occurrence().data()) {
          return Fail(ErrDuplicateTableFieldName, member_used.name, original_name, previous_span);
        }
        return Fail(ErrDuplicateTableFieldNameCanonical, member_used.name, original_name,
                    previous_span.data(), previous_span, canonical_name);
      }
      if (!CompileTypeConstructor(member_used.type_ctor.get())) {
        return false;
      }
      if (!first_resource_member && IsResourceType(member_used.type_ctor->type)) {
        first_resource_member = &member_used;
      }
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    return Fail(ErrNonDenseOrdinal, span, ordinal);
  }

  if (first_resource_member && table_declaration->resourceness == types::Resourceness::kValue) {
    return Fail(ErrResourceTypeInValueType, first_resource_member->name,
                first_resource_member->type_ctor->type, table_declaration->name,
                first_resource_member->name.data(), table_declaration->name);
  }

  return true;
}

bool Library::CompileUnion(Union* union_declaration) {
  Scope<std::string> scope;
  Ordinal64Scope ordinal_scope;
  const Union::Member::Used* first_resource_member = nullptr;

  for (const auto& member : union_declaration->members) {
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      return Fail(ErrDuplicateUnionMemberOrdinal, member.ordinal->span(),
                  ordinal_result.previous_occurrence());
    }
    if (member.maybe_used) {
      const auto& member_used = *member.maybe_used;
      const auto original_name = member_used.name.data();
      const auto canonical_name = utils::canonicalize(original_name);
      const auto name_result = scope.Insert(canonical_name, member_used.name);
      if (!name_result.ok()) {
        const auto previous_span = name_result.previous_occurrence();
        if (original_name == name_result.previous_occurrence().data()) {
          return Fail(ErrDuplicateUnionMemberName, member_used.name, original_name, previous_span);
        }
        return Fail(ErrDuplicateUnionMemberNameCanonical, member_used.name, original_name,
                    previous_span.data(), previous_span, canonical_name);
      }

      if (!CompileTypeConstructor(member_used.type_ctor.get())) {
        return false;
      }
      if (!first_resource_member && IsResourceType(member_used.type_ctor->type)) {
        first_resource_member = &member_used;
      }
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    return Fail(ErrNonDenseOrdinal, span, ordinal);
  }

  if (first_resource_member && union_declaration->resourceness == types::Resourceness::kValue) {
    return Fail(ErrResourceTypeInValueType, first_resource_member->name,
                first_resource_member->type_ctor->type, union_declaration->name,
                first_resource_member->name.data(), union_declaration->name);
  }

  {
    std::vector<const Union::Member::Used*> used_members;
    for (const auto& member : union_declaration->members) {
      if (member.maybe_used)
        used_members.push_back(member.maybe_used.get());
    }

    auto err = ValidateUnknownConstraints(*union_declaration, union_declaration->strictness,
                                          &used_members);
    if (err) {
      return Fail(std::move(err));
    }
  }

  return true;
}

bool Library::CompileTypeAlias(TypeAlias* decl) {
  // Since type aliases can have partial type constructors, it's not always
  // possible to compile them based solely on their declaration.
  //
  // For instance, we might have
  //
  //     using alias = vector:5;
  //
  //  which is only valid on use `alias<string>`.
  //
  // We temporarily disable error reporting, and attempt to compile the
  // partial type constructor.
  bool partial_type_ctor_compiled = false;
  {
    auto temporary_mode = reporter_->OverrideMode(Reporter::ReportingMode::kDoNotReport);
    partial_type_ctor_compiled = CompileTypeConstructor(decl->partial_type_ctor.get());
  }
  if (decl->partial_type_ctor->maybe_arg_type_ctor && !partial_type_ctor_compiled) {
    if (!CompileTypeConstructor(decl->partial_type_ctor->maybe_arg_type_ctor.get()))
      return false;
  }
  return ResolveSizeBound(decl->partial_type_ctor.get(), nullptr /* out_size */);
}

bool Library::Compile() {
  if (!SortDeclarations()) {
    return false;
  }

  // We process declarations in topologically sorted order. For
  // example, we process a struct member's type before the entire
  // struct.
  auto compile_step = StartCompileStep();
  for (Decl* decl : declaration_order_) {
    compile_step.ForDecl(decl);
  }
  if (!compile_step.Done())
    return false;

  auto verify_attributes_step = StartVerifyAttributesStep();
  for (Decl* decl : declaration_order_) {
    verify_attributes_step.ForDecl(decl);
  }
  if (!verify_attributes_step.Done())
    return false;

  if (!dependencies_.VerifyAllDependenciesWereUsed(*this, reporter_))
    return false;

  return reporter_->errors().size() == 0;
}

bool Library::CompileTypeConstructor(TypeConstructor* type_ctor) {
  const Type* maybe_arg_type = nullptr;
  if (type_ctor->maybe_arg_type_ctor != nullptr) {
    if (!CompileTypeConstructor(type_ctor->maybe_arg_type_ctor.get()))
      return false;
    maybe_arg_type = type_ctor->maybe_arg_type_ctor->type;
  }
  const Size* size = nullptr;
  if (!ResolveSizeBound(type_ctor, &size)) {
    return false;
  }

  std::optional<types::HandleSubtype> handle_subtype;
  if (type_ctor->handle_subtype) {
    assert(!type_ctor->handle_subtype_identifier &&
           "cannot have both new and old style handle syntax");
    handle_subtype = type_ctor->handle_subtype;
  } else if (type_ctor->handle_subtype_identifier) {
    types::HandleSubtype subtype;
    if (!ResolveHandleSubtypeIdentifier(type_ctor, &subtype)) {
      return Fail(ErrCouldNotResolveHandleSubtype, type_ctor->name.span(),
                  type_ctor->handle_subtype_identifier.value());
    }
    handle_subtype = subtype;
  }

  if (type_ctor->handle_rights)
    if (!ResolveConstant(type_ctor->handle_rights.get(), &kRightsType))
      return Fail(ErrCouldNotResolveHandleRights);

  if (!typespace_->Create(type_ctor->name, maybe_arg_type, handle_subtype,
                          type_ctor->handle_rights.get(), size, type_ctor->nullability,
                          &type_ctor->type, &type_ctor->from_type_alias))
    return false;

  return true;
}

bool Library::ResolveHandleSubtypeIdentifier(TypeConstructor* type_ctor,
                                             types::HandleSubtype* subtype) {
  assert(type_ctor->handle_subtype_identifier);

  // We only support an extremely limited form of resource suitable for
  // handles here, where it must be:
  // - derived from uint32
  // - have a single properties element
  // - the single property element must be a reference to an enum
  // - the single property must be named "subtype".

  Decl* handle_decl = LookupDeclByName(type_ctor->name);
  if (!handle_decl || handle_decl->kind != Decl::Kind::kResource) {
    return Fail(ErrHandleSubtypeNotResource, type_ctor->name.span(), type_ctor->name);
  }

  auto* resource = static_cast<Resource*>(handle_decl);
  if (!resource->subtype_ctor || resource->subtype_ctor->name.full_name() != "uint32") {
    return Fail(ErrResourceMustBeUint32Derived, type_ctor->name.span(), resource->name);
  }
  if (resource->properties.size() != 1 || resource->properties[0].name.data() != "subtype") {
    return Fail(ErrResourceCanOnlyHaveSubtypeProperty, type_ctor->name.span(), resource->name);
  }

  Decl* subtype_decl = LookupDeclByName(resource->properties[0].type_ctor->name);
  if (!subtype_decl || subtype_decl->kind != Decl::Kind::kEnum) {
    return Fail(ErrResourceSubtypePropertyMustReferToEnum, type_ctor->name.span(), resource->name);
  }

  auto* subtype_enum = static_cast<Enum*>(subtype_decl);
  for (const auto& member : subtype_enum->members) {
    if (member.name.data() == type_ctor->handle_subtype_identifier->span()->data()) {
      if (!ResolveConstant(member.value.get(), &kHandleSubtypeType)) {
        return false;
      }
      const flat::ConstantValue& value = member.value->Value();
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value);
      *subtype = types::HandleSubtype(static_cast<uint32_t>(numeric_constant));
      return true;
    }
  }

  return false;
}

bool Library::ResolveSizeBound(TypeConstructor* type_ctor, const Size** out_size) {
  if (!type_ctor->maybe_size) {
    if (out_size) {
      *out_size = nullptr;
    }
    return true;
  }

  Constant* size_constant = type_ctor->maybe_size.get();
  if (!ResolveConstant(size_constant, &kSizeType)) {
    if (size_constant->kind == Constant::Kind::kIdentifier) {
      auto name = static_cast<IdentifierConstant*>(size_constant)->name;
      if (name.library() == this && name.decl_name() == "MAX" && !name.member_name()) {
        size_constant->ResolveTo(std::make_unique<Size>(Size::Max()));
      }
    }
  }
  if (!size_constant->IsResolved()) {
    return Fail(ErrCouldNotParseSizeBound, type_ctor->name.span());
  }
  if (out_size) {
    *out_size = static_cast<const Size*>(&size_constant->Value());
  }
  return true;
}

template <typename DeclType, typename MemberType>
bool Library::ValidateMembers(DeclType* decl, MemberValidator<MemberType> validator) {
  assert(decl != nullptr);

  constexpr const char* decl_type = std::is_same_v<DeclType, Enum> ? "enum" : "bits";

  Scope<std::string> name_scope;
  Scope<MemberType> value_scope;
  bool success = true;
  for (const auto& member : decl->members) {
    assert(member.value != nullptr && "Compiler bug: member value is null!");

    if (!ResolveConstant(member.value.get(), decl->subtype_ctor->type)) {
      return Fail(ErrCouldNotResolveMember, member.name, std::string(decl_type));
    }

    // Check that the member identifier hasn't been used yet
    const auto original_name = member.name.data();
    const auto canonical_name = utils::canonicalize(original_name);
    const auto name_result = name_scope.Insert(canonical_name, member.name);
    if (!name_result.ok()) {
      const auto previous_span = name_result.previous_occurrence();
      // We can log the error and then continue validating for other issues in the decl
      if (original_name == name_result.previous_occurrence().data()) {
        success = Fail(ErrDuplicateMemberName, member.name, std::string_view(decl_type),
                       original_name, previous_span);
      } else {
        success = Fail(ErrDuplicateMemberNameCanonical, member.name, std::string_view(decl_type),
                       original_name, previous_span.data(), previous_span, canonical_name);
      }
    }

    MemberType value =
        static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    const auto value_result = value_scope.Insert(value, member.name);
    if (!value_result.ok()) {
      const auto previous_span = value_result.previous_occurrence();
      // We can log the error and then continue validating other members for other bugs
      success = Fail(ErrDuplicateMemberValue, member.name, std::string_view(decl_type),
                     original_name, previous_span.data(), previous_span);
    }

    auto err = validator(value, member.attributes.get());
    if (err) {
      err->span = member.name;
      success = Fail(std::move(err));
    }
  }

  return success;
}

template <typename T>
static bool IsPowerOfTwo(T t) {
  if (t == 0) {
    return false;
  }
  if ((t & (t - 1)) != 0) {
    return false;
  }
  return true;
}

template <typename MemberType>
bool Library::ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask) {
  static_assert(std::is_unsigned<MemberType>::value && !std::is_same<MemberType, bool>::value,
                "Bits members must be an unsigned integral type!");
  // Each bits member must be a power of two.
  MemberType mask = 0u;
  auto validator = [&mask](MemberType member,
                           const raw::AttributeList*) -> std::unique_ptr<Diagnostic> {
    if (!IsPowerOfTwo(member)) {
      return Reporter::MakeError(ErrBitsMemberMustBePowerOfTwo);
    }
    mask |= member;
    return nullptr;
  };
  if (!ValidateMembers<Bits, MemberType>(bits_decl, validator)) {
    return false;
  }
  *out_mask = mask;
  return true;
}

template <typename MemberType>
bool Library::ValidateEnumMembersAndCalcUnknownValue(Enum* enum_decl,
                                                     MemberType* out_unknown_value) {
  static_assert(std::is_integral<MemberType>::value && !std::is_same<MemberType, bool>::value,
                "Enum members must be an integral type!");

  auto unknown_value = std::numeric_limits<MemberType>::max();
  for (const auto& member : enum_decl->members) {
    if (!ResolveConstant(member.value.get(), enum_decl->subtype_ctor->type)) {
      return Fail(ErrCouldNotResolveMember, member.name, std::string("enum"));
    }
    auto attributes = member.attributes.get();
    if (attributes && attributes->HasAttribute("Unknown")) {
      unknown_value =
          static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    }
  }
  *out_unknown_value = unknown_value;

  auto validator = [enum_decl, unknown_value](
                       MemberType member,
                       const raw::AttributeList* attributes) -> std::unique_ptr<Diagnostic> {
    switch (enum_decl->strictness) {
      case types::Strictness::kFlexible:
        break;
      case types::Strictness::kStrict:
        // Strict enums cannot have [Unknown] attributes on members, but that will be validated by
        // ValidateUnknownConstraints() (called later in this method).
        return nullptr;
    }

    if (member != unknown_value)
      return nullptr;

    if (attributes && attributes->HasAttribute("Unknown"))
      return nullptr;

    return Reporter::MakeError(ErrFlexibleEnumMemberWithMaxValue, std::to_string(unknown_value),
                               std::to_string(unknown_value), std::to_string(unknown_value),
                               std::to_string(unknown_value));
  };

  if (!ValidateMembers<Enum, MemberType>(enum_decl, validator))
    return false;

  {
    std::vector<const Enum::Member*> members;
    members.reserve(enum_decl->members.size());
    for (const auto& member : enum_decl->members) {
      members.push_back(&member);
    }

    auto err = ValidateUnknownConstraints(*enum_decl, enum_decl->strictness, &members);
    if (err) {
      return Fail(std::move(err));
    }
  }

  return true;
}

bool Library::HasAttribute(std::string_view name) const {
  if (!attributes_)
    return false;
  return attributes_->HasAttribute(std::string(name));
}

const std::set<Library*>& Library::dependencies() const { return dependencies_.dependencies(); }

std::unique_ptr<TypeConstructor> TypeConstructor::CreateSizeType() {
  return std::make_unique<TypeConstructor>(
      Name::CreateIntrinsic("uint32"), nullptr /* maybe_arg_type */,
      std::optional<types::HandleSubtype>(), std::optional<Name>() /* handle_subtype_identifier */,
      nullptr /* handle_rights */, nullptr /* maybe_size */, types::Nullability::kNonnullable);
}

}  // namespace flat
}  // namespace fidl
