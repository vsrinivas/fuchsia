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
#include "fidl/lexer.h"
#include "fidl/names.h"
#include "fidl/ordinals.h"
#include "fidl/parser.h"
#include "fidl/raw_ast.h"
#include "fidl/utils.h"

namespace fidl {
namespace flat {

namespace {

class ScopeInsertResult {
 public:
  explicit ScopeInsertResult(std::unique_ptr<SourceLocation> previous_occurrence)
      : previous_occurrence_(std::move(previous_occurrence)) {}

  static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
  static ScopeInsertResult FailureAt(SourceLocation previous) {
    return ScopeInsertResult(std::make_unique<SourceLocation>(previous));
  }

  bool ok() const { return previous_occurrence_ == nullptr; }

  const SourceLocation& previous_occurrence() const {
    assert(!ok());
    return *previous_occurrence_;
  }

 private:
  std::unique_ptr<SourceLocation> previous_occurrence_;
};

template <typename T>
class Scope {
 public:
  ScopeInsertResult Insert(const T& t, SourceLocation location) {
    auto iter = scope_.find(t);
    if (iter != scope_.end()) {
      return ScopeInsertResult::FailureAt(iter->second);
    } else {
      scope_.emplace(t, location);
      return ScopeInsertResult::Ok();
    }
  }

  typename std::map<T, SourceLocation>::const_iterator begin() const { return scope_.begin(); }

  typename std::map<T, SourceLocation>::const_iterator end() const { return scope_.end(); }

 private:
  std::map<T, SourceLocation> scope_;
};

using Ordinal32Scope = Scope<uint32_t>;
using Ordinal64Scope = Scope<uint64_t>;

std::optional<std::pair<uint32_t, SourceLocation>> FindFirstNonDenseOrdinal(
    const Ordinal32Scope& scope) {
  uint32_t last_ordinal_seen = 0;
  for (const auto& ordinal_and_loc : scope) {
    uint32_t next_expected_ordinal = last_ordinal_seen + 1;
    if (ordinal_and_loc.first != next_expected_ordinal) {
      return std::optional{std::make_pair(next_expected_ordinal, ordinal_and_loc.second)};
    }
    last_ordinal_seen = ordinal_and_loc.first;
  }
  return std::nullopt;
}

struct MethodScope {
  Ordinal64Scope ordinals;
  Scope<std::string_view> names;
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

std::string Decl::GetName() const { return std::string(name.name_part()); }

const std::set<std::pair<std::string, std::string_view>> allowed_simple_unions{{
    {"fuchsia.io", "NodeInfo"},
}};

bool IsSimple(const Type* type, const TypeShape& typeshape, ErrorReporter* error_reporter) {
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
      return typeshape.Depth() == 0u;
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      if (identifier_type->type_decl->kind == Decl::Kind::kUnion) {
        auto union_name = std::make_pair<const std::string&, const std::string_view&>(
            LibraryName(identifier_type->name.library(), "."), identifier_type->name.name_part());
        if (allowed_simple_unions.find(union_name) == allowed_simple_unions.end()) {
          // Any unions not in the allow-list are treated as non-simple.
          std::string message("union '");
          message.append(identifier_type->name.name_part());
          message.append("' is not allowed to be simple");
          error_reporter->ReportError(identifier_type->name.maybe_location(), message);
          return false;
        }
      }
      switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
          // If the identifier is nullable, then we can handle a depth of 1
          // because the secondary object is directly accessible.
          return typeshape.Depth() <= 1u;
        case types::Nullability::kNonnullable:
          return typeshape.Depth() == 0u;
      }
    }
  }
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

FieldShape XUnion::Member::Used::fieldshape(WireFormat wire_format) const {
  return FieldShape(*this, wire_format);
}

uint32_t Union::DataOffset(WireFormat wire_format) const {
  assert(wire_format == WireFormat::kOld && "no union data offset concept for other formats");
  return TypeShape(*this, wire_format).Alignment();
}

std::vector<std::reference_wrapper<const Union::Member>> Union::MembersSortedByXUnionOrdinal()
    const {
  std::vector<std::reference_wrapper<const Member>> sorted_members(members.cbegin(),
                                                                   members.cend());
  std::sort(sorted_members.begin(), sorted_members.end(),
            [](const auto& member1, const auto& member2) {
              return member1.get().xunion_ordinal->value < member2.get().xunion_ordinal->value;
            });
  return sorted_members;
}

bool Typespace::Create(const flat::Name& name, const Type* arg_type,
                       const std::optional<types::HandleSubtype>& handle_subtype, const Size* size,
                       types::Nullability nullability, const Type** out_type,
                       std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) {
  std::unique_ptr<Type> type;
  if (!CreateNotOwned(name, arg_type, handle_subtype, size, nullability, &type,
                      out_from_type_alias))
    return false;
  types_.push_back(std::move(type));
  *out_type = types_.back().get();
  return true;
}

bool Typespace::CreateNotOwned(const flat::Name& name, const Type* arg_type,
                               const std::optional<types::HandleSubtype>& handle_subtype,
                               const Size* size, types::Nullability nullability,
                               std::unique_ptr<Type>* out_type,
                               std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) {
  // TODO(pascallouis): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name,
  // arg_type, size, and nullability.

  auto maybe_location = name.maybe_location();
  auto type_template = LookupTemplate(name);
  if (type_template == nullptr) {
    std::string message("unknown type ");
    message.append(name.name_full());
    error_reporter_->ReportError(maybe_location, message);
    return false;
  }
  return type_template->Create(maybe_location, arg_type, handle_subtype, size, nullability,
                               out_type, out_from_type_alias);
}

void Typespace::AddTemplate(std::unique_ptr<TypeTemplate> type_template) {
  templates_.emplace(type_template->name(), std::move(type_template));
}

const TypeTemplate* Typespace::LookupTemplate(const flat::Name& name) const {
  Name global_name(nullptr, std::string(name.name_part()));
  auto iter1 = templates_.find(&global_name);
  if (iter1 != templates_.end())
    return iter1->second.get();

  auto iter2 = templates_.find(&name);
  if (iter2 != templates_.end())
    return iter2->second.get();

  return nullptr;
}

bool TypeTemplate::Fail(const SourceLocation* maybe_location, const std::string& content) const {
  std::string message(NameFlatName(name_));
  message.append(" ");
  message.append(content);
  error_reporter_->ReportError(maybe_location, message);
  return false;
}

class PrimitiveTypeTemplate : public TypeTemplate {
 public:
  PrimitiveTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter,
                        const std::string& name, types::PrimitiveSubtype subtype)
      : TypeTemplate(Name(nullptr, name), typespace, error_reporter), subtype_(subtype) {}

  bool Create(const SourceLocation* maybe_location, const Type* maybe_arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* maybe_size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (maybe_arg_type != nullptr)
      return CannotBeParameterized(maybe_location);
    if (maybe_size != nullptr)
      return CannotHaveSize(maybe_location);
    if (nullability == types::Nullability::kNullable)
      return CannotBeNullable(maybe_location);

    *out_type = std::make_unique<PrimitiveType>(name_, subtype_);
    return true;
  }

 private:
  const types::PrimitiveSubtype subtype_;
};

class BytesTypeTemplate final : public TypeTemplate {
 public:
  BytesTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "vector"), typespace, error_reporter), uint8_type_(kUint8Type) {}

  bool Create(const SourceLocation* maybe_location, const Type* maybe_arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (maybe_arg_type != nullptr)
      return CannotBeParameterized(maybe_location);
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<VectorType>(name_, &uint8_type_, size, nullability);
    return true;
  }

 private:
  // TODO(FIDL-389): Remove when canonicalizing types.
  const Name kUint8TypeName = Name(nullptr, "uint8");
  const PrimitiveType kUint8Type = PrimitiveType(kUint8TypeName, types::PrimitiveSubtype::kUint8);

  const PrimitiveType uint8_type_;
  Size max_size = Size::Max();
};

class ArrayTypeTemplate final : public TypeTemplate {
 public:
  ArrayTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "array"), typespace, error_reporter) {}

  bool Create(const SourceLocation* maybe_location, const Type* arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (arg_type == nullptr)
      return MustBeParameterized(maybe_location);
    if (size == nullptr)
      return MustHaveSize(maybe_location);
    if (size->value == 0)
      return MustHaveNonZeroSize(maybe_location);
    if (nullability == types::Nullability::kNullable)
      return CannotBeNullable(maybe_location);

    *out_type = std::make_unique<ArrayType>(name_, arg_type, size);
    return true;
  }
};

class VectorTypeTemplate final : public TypeTemplate {
 public:
  VectorTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "vector"), typespace, error_reporter) {}

  bool Create(const SourceLocation* maybe_location, const Type* arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (arg_type == nullptr)
      return MustBeParameterized(maybe_location);
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<VectorType>(name_, arg_type, size, nullability);
    return true;
  }

 private:
  Size max_size = Size::Max();
};

class StringTypeTemplate final : public TypeTemplate {
 public:
  StringTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "string"), typespace, error_reporter) {}

  bool Create(const SourceLocation* maybe_location, const Type* arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (arg_type != nullptr)
      return CannotBeParameterized(maybe_location);
    if (size == nullptr)
      size = &max_size;

    *out_type = std::make_unique<StringType>(name_, size, nullability);
    return true;
  }

 private:
  Size max_size = Size::Max();
};

class HandleTypeTemplate final : public TypeTemplate {
 public:
  HandleTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "handle"), typespace, error_reporter) {}

  bool Create(const SourceLocation* maybe_location, const Type* maybe_arg_type,
              const std::optional<types::HandleSubtype>& opt_handle_subtype, const Size* maybe_size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(maybe_arg_type == nullptr);

    if (maybe_size != nullptr)
      return CannotHaveSize(maybe_location);

    auto handle_subtype = opt_handle_subtype.value_or(types::HandleSubtype::kHandle);

    *out_type = std::make_unique<HandleType>(name_, handle_subtype, nullability);
    return true;
  }
};

class RequestTypeTemplate final : public TypeTemplate {
 public:
  RequestTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
      : TypeTemplate(Name(nullptr, "request"), typespace, error_reporter) {}

  bool Create(const SourceLocation* maybe_location, const Type* arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* maybe_size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    if (arg_type == nullptr)
      return MustBeParameterized(maybe_location);
    if (arg_type->kind != Type::Kind::kIdentifier)
      return Fail(maybe_location, "must be a protocol");
    auto protocol_type = static_cast<const IdentifierType*>(arg_type);
    if (protocol_type->type_decl->kind != Decl::Kind::kProtocol)
      return Fail(maybe_location, "must be a protocol");
    if (maybe_size != nullptr)
      return CannotHaveSize(maybe_location);

    *out_type = std::make_unique<RequestHandleType>(name_, protocol_type, nullability);
    return true;
  }

 private:
  // TODO(pascallouis): Make Min/Max an actual value on NumericConstantValue
  // class, to simply write &Size::Max() above.
  Size max_size = Size::Max();
};

class TypeDeclTypeTemplate final : public TypeTemplate {
 public:
  TypeDeclTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                       Library* library, TypeDecl* type_decl)
      : TypeTemplate(std::move(name), typespace, error_reporter),
        library_(library),
        type_decl_(type_decl) {}

  bool Create(const SourceLocation* maybe_location, const Type* arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* size,
              types::Nullability nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

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
        return Fail(maybe_location, "cannot use services in other declarations");

      case Decl::Kind::kProtocol:
        break;

      case Decl::Kind::kXUnion:
        // Do nothing here: nullable XUnions have the same encoding
        // representation as non-optional XUnions (i.e. nullable XUnions are
        // inlined).
        break;

      case Decl::Kind::kEnum:
      case Decl::Kind::kTable:
        if (nullability == types::Nullability::kNullable)
          return CannotBeNullable(maybe_location);
        break;

      default:
        if (nullability == types::Nullability::kNullable)
          break;
    }

    *out_type = std::make_unique<IdentifierType>(name_, nullability, type_decl_);
    return true;
  }

 private:
  Library* library_;
  TypeDecl* type_decl_;
};

class TypeAliasTypeTemplate final : public TypeTemplate {
 public:
  TypeAliasTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                        Library* library, TypeAlias* decl)
      : TypeTemplate(std::move(name), typespace, error_reporter), decl_(decl) {}

  bool Create(const SourceLocation* maybe_location, const Type* maybe_arg_type,
              const std::optional<types::HandleSubtype>& no_handle_subtype, const Size* maybe_size,
              types::Nullability maybe_nullability, std::unique_ptr<Type>* out_type,
              std::optional<TypeConstructor::FromTypeAlias>* out_from_type_alias) const {
    assert(!no_handle_subtype);

    const Type* arg_type = nullptr;
    if (decl_->partial_type_ctor->maybe_arg_type_ctor) {
      if (maybe_arg_type) {
        return Fail(maybe_location, "cannot parametrize twice");
      }
      arg_type = decl_->partial_type_ctor->maybe_arg_type_ctor->type;
    } else {
      arg_type = maybe_arg_type;
    }

    const Size* size = nullptr;
    if (decl_->partial_type_ctor->maybe_size) {
      if (maybe_size) {
        return Fail(maybe_location, "cannot bound twice");
      }
      size = static_cast<const Size*>(&decl_->partial_type_ctor->maybe_size->Value());
    } else {
      size = maybe_size;
    }

    types::Nullability nullability;
    if (decl_->partial_type_ctor->nullability == types::Nullability::kNullable) {
      if (maybe_nullability == types::Nullability::kNullable) {
        return Fail(maybe_location, "cannot indicate nullability twice");
      }
      nullability = types::Nullability::kNullable;
    } else {
      nullability = maybe_nullability;
    }

    if (!typespace_->CreateNotOwned(decl_->partial_type_ctor->name, arg_type,
                                    // TODO(pascallouis): Oops, that's wrong. Need to pass handle
                                    // parametrization down.
                                    std::optional<types::HandleSubtype>(), size, nullability,
                                    out_type, nullptr))
      return false;
    if (out_from_type_alias)
      *out_from_type_alias =
          TypeConstructor::FromTypeAlias(decl_, maybe_arg_type, maybe_size, maybe_nullability);
    return true;
  }

 private:
  TypeAlias* decl_;
};

Typespace Typespace::RootTypes(ErrorReporter* error_reporter) {
  Typespace root_typespace(error_reporter);

  auto add_template = [&](std::unique_ptr<TypeTemplate> type_template) {
    auto name = type_template->name();
    root_typespace.templates_.emplace(name, std::move(type_template));
  };

  auto add_primitive = [&](const std::string& name, types::PrimitiveSubtype subtype) {
    add_template(
        std::make_unique<PrimitiveTypeTemplate>(&root_typespace, error_reporter, name, subtype));
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
  const static auto kByteName = Name(nullptr, "byte");
  const static auto kBytesName = Name(nullptr, "bytes");
  root_typespace.templates_.emplace(
      &kByteName, std::make_unique<PrimitiveTypeTemplate>(&root_typespace, error_reporter, "uint8",
                                                          types::PrimitiveSubtype::kUint8));
  root_typespace.templates_.emplace(
      &kBytesName, std::make_unique<BytesTypeTemplate>(&root_typespace, error_reporter));

  add_template(std::make_unique<ArrayTypeTemplate>(&root_typespace, error_reporter));
  add_template(std::make_unique<VectorTypeTemplate>(&root_typespace, error_reporter));
  add_template(std::make_unique<StringTypeTemplate>(&root_typespace, error_reporter));
  add_template(std::make_unique<HandleTypeTemplate>(&root_typespace, error_reporter));
  add_template(std::make_unique<RequestTypeTemplate>(&root_typespace, error_reporter));

  return root_typespace;
}

AttributeSchema::AttributeSchema(const std::set<Placement>& allowed_placements,
                                 const std::set<std::string> allowed_values, Constraint constraint)
    : allowed_placements_(allowed_placements),
      allowed_values_(allowed_values),
      constraint_(std::move(constraint)) {}

void AttributeSchema::ValidatePlacement(ErrorReporter* error_reporter,
                                        const raw::Attribute& attribute,
                                        Placement placement) const {
  if (allowed_placements_.size() == 0)
    return;
  auto iter = allowed_placements_.find(placement);
  if (iter != allowed_placements_.end())
    return;
  std::string message("placement of attribute '");
  message.append(attribute.name);
  message.append("' disallowed here");
  error_reporter->ReportError(attribute.location(), message);
}

void AttributeSchema::ValidateValue(ErrorReporter* error_reporter,
                                    const raw::Attribute& attribute) const {
  if (allowed_values_.size() == 0)
    return;
  auto iter = allowed_values_.find(attribute.value);
  if (iter != allowed_values_.end())
    return;
  std::string message("attribute '");
  message.append(attribute.name);
  message.append("' has invalid value '");
  message.append(attribute.value);
  message.append("', should be one of '");
  bool first = true;
  for (const auto& hint : allowed_values_) {
    if (!first)
      message.append(", ");
    message.append(hint);
    message.append("'");
    first = false;
  }
  error_reporter->ReportError(attribute.location(), message);
}

void AttributeSchema::ValidateConstraint(ErrorReporter* error_reporter,
                                         const raw::Attribute& attribute, const Decl* decl) const {
  auto check = error_reporter->Checkpoint();
  auto passed = constraint_(error_reporter, attribute, decl);
  if (passed) {
    assert(check.NoNewErrors() && "cannot add errors and pass");
  } else if (check.NoNewErrors()) {
    std::string message("declaration did not satisfy constraint of attribute '");
    message.append(attribute.name);
    message.append("' with value '");
    message.append(attribute.value);
    message.append("'");
    // TODO(pascallouis): It would be nicer to use the location of
    // the declaration, however we do not keep it around today.
    error_reporter->ReportError(attribute.location(), message);
  }
}

bool SimpleLayoutConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                            const Decl* decl) {
  assert(decl->kind == Decl::Kind::kStruct);
  auto struct_decl = static_cast<const Struct*>(decl);
  bool ok = true;
  for (const auto& member : struct_decl->members) {
    if (!IsSimple(member.type_ctor.get()->type, member.typeshape(WireFormat::kOld),
                  error_reporter)) {
      std::string message("member '");
      message.append(member.name.data());
      message.append("' is not simple");
      error_reporter->ReportError(member.name, message);
      ok = false;
    }
  }
  return ok;
}

bool ParseBound(ErrorReporter* error_reporter, const SourceLocation& location,
                const std::string& input, uint32_t* out_value) {
  auto result = utils::ParseNumeric(input, out_value, 10);
  switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
      error_reporter->ReportError(location, "bound is too big");
      return false;
    case utils::ParseNumericResult::kMalformed: {
      std::string message("unable to parse bound '");
      message.append(input);
      message.append("'");
      error_reporter->ReportError(location, message);
      return false;
    }
    case utils::ParseNumericResult::kSuccess:
      return true;
  }
}

// TODO(fxb/38601): Move to new format once migraiton is done.
bool MaxBytesConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                        const Decl* decl) {
  uint32_t bound;
  if (!ParseBound(error_reporter, attribute.location(), attribute.value, &bound))
    return false;
  uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      max_bytes = struct_decl->typeshape(WireFormat::kOld).InlineSize() +
                  struct_decl->typeshape(WireFormat::kOld).MaxOutOfLine();
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      max_bytes = table_decl->typeshape(WireFormat::kOld).InlineSize() +
                  table_decl->typeshape(WireFormat::kOld).MaxOutOfLine();
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      max_bytes = union_decl->typeshape(WireFormat::kOld).InlineSize() +
                  union_decl->typeshape(WireFormat::kOld).MaxOutOfLine();
      break;
    }
    case Decl::Kind::kXUnion: {
      auto xunion_decl = static_cast<const XUnion*>(decl);
      max_bytes = xunion_decl->typeshape(WireFormat::kOld).InlineSize() +
                  xunion_decl->typeshape(WireFormat::kOld).MaxOutOfLine();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_bytes > bound) {
    std::ostringstream message;
    message << "too large: only ";
    message << bound;
    message << " bytes allowed, but ";
    message << max_bytes;
    message << " bytes found";
    error_reporter->ReportError(attribute.location(), message.str());
    return false;
  }
  return true;
}

// TODO(fxb/38601): Move to new format once migraiton is done.
bool MaxHandlesConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                          const Decl* decl) {
  uint32_t bound;
  if (!ParseBound(error_reporter, attribute.location(), attribute.value.c_str(), &bound))
    return false;
  uint32_t max_handles = std::numeric_limits<uint32_t>::max();
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      max_handles = struct_decl->typeshape(WireFormat::kOld).MaxHandles();
      break;
    }
    case Decl::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(decl);
      max_handles = table_decl->typeshape(WireFormat::kOld).MaxHandles();
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(decl);
      max_handles = union_decl->typeshape(WireFormat::kOld).MaxHandles();
      break;
    }
    case Decl::Kind::kXUnion: {
      auto xunion_decl = static_cast<const XUnion*>(decl);
      max_handles = xunion_decl->typeshape(WireFormat::kOld).MaxHandles();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_handles > bound) {
    std::ostringstream message;
    message << "too many handles: only ";
    message << bound;
    message << " allowed, but ";
    message << max_handles;
    message << " found";
    error_reporter->ReportError(attribute.location(), message.str());
    return false;
  }
  return true;
}

bool ResultShapeConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                           const Decl* decl) {
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
    error_reporter->ReportError(decl->name.maybe_location(),
                                "invalid error type: must be int32, uint32 or an enum therof");
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

bool TransportConstraint(ErrorReporter* error_reporter, const raw::Attribute& attribute,
                         const Decl* decl) {
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
      std::ostringstream out;
      out << "invalid transport type: got " << transport << " expected one of ";
      bool first = true;
      for (const auto& t : *kValidTransports) {
        if (!first) {
          out << ", ";
        }
        first = false;
        out << t;
      }
      error_reporter->ReportError(decl->name.maybe_location(), out.str());
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
  AddAttributeSchema("FragileBase", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
  }, {
    "",
  }));
  AddAttributeSchema("Layout", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
  }, {
    "Simple",
  },
  SimpleLayoutConstraint));
  AddAttributeSchema("MaxBytes", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
    AttributeSchema::Placement::kMethod,
    AttributeSchema::Placement::kStructDecl,
    AttributeSchema::Placement::kTableDecl,
    AttributeSchema::Placement::kUnionDecl,
    AttributeSchema::Placement::kXUnionDecl,
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
    AttributeSchema::Placement::kXUnionDecl,
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
    AttributeSchema::Placement::kXUnionMember,
  }, {
      /* any value */
  }));
  AddAttributeSchema("Transport", AttributeSchema({
    AttributeSchema::Placement::kProtocolDecl,
  }, {
    /* any value */
  }, TransportConstraint));
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

const AttributeSchema* Libraries::RetrieveAttributeSchema(ErrorReporter* error_reporter,
                                                          const raw::Attribute& attribute) const {
  const auto& attribute_name = attribute.name;
  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    const auto& schema = iter->second;
    return &schema;
  }

  // Skip typo check?
  if (error_reporter == nullptr)
    return nullptr;

  // Match against all known attributes.
  for (const auto& name_and_schema : attribute_schemas_) {
    auto edit_distance = EditDistance(name_and_schema.first, attribute_name);
    if (0 < edit_distance && edit_distance < 2) {
      std::string message("suspect attribute with name '");
      message.append(attribute_name);
      message.append("'; did you mean '");
      message.append(name_and_schema.first);
      message.append("'?");
      error_reporter->ReportWarning(attribute.location(), message);
      return nullptr;
    }
  }

  return nullptr;
}

bool Dependencies::Register(const SourceLocation& location, std::string_view filename,
                            Library* dep_library,
                            const std::unique_ptr<raw::Identifier>& maybe_alias) {
  refs_.push_back(std::make_unique<LibraryRef>(location, dep_library));
  auto ref = refs_.back().get();

  auto library_name = dep_library->name();
  if (!InsertByName(filename, library_name, ref)) {
    return false;
  }

  if (maybe_alias) {
    std::vector<std::string_view> alias_name = {maybe_alias->location().data()};
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

bool Dependencies::VerifyAllDependenciesWereUsed(const Library& for_library,
                                                 ErrorReporter* error_reporter) {
  auto checkpoint = error_reporter->Checkpoint();
  for (auto by_name_iter = dependencies_.begin(); by_name_iter != dependencies_.end();
       by_name_iter++) {
    const auto& by_name = *by_name_iter->second;
    for (const auto& name_to_ref : by_name) {
      const auto& ref = name_to_ref.second;
      if (ref->used_)
        continue;
      std::string message = "Library ";
      message.append(NameLibrary(for_library.name()));
      message.append(" imports ");
      message.append(NameLibrary(ref->library_->name()));
      message.append(" but does not use it. Either use ");
      message.append(NameLibrary(ref->library_->name()));
      message.append(", or remove import.");
      error_reporter->ReportError(ref->location_, message);
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

bool Library::Fail(std::string_view message) {
  error_reporter_->ReportError(message);
  return false;
}

bool Library::Fail(const SourceLocation* maybe_location, std::string_view message) {
  error_reporter_->ReportError(maybe_location, message);
  return false;
}

void Library::ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                          const raw::AttributeList* attributes) {
  if (attributes == nullptr)
    return;
  for (const auto& attribute : attributes->attributes) {
    auto schema = all_libraries_->RetrieveAttributeSchema(error_reporter_, attribute);
    if (schema != nullptr) {
      schema->ValidatePlacement(error_reporter_, attribute, placement);
      schema->ValidateValue(error_reporter_, attribute);
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
      schema->ValidateConstraint(error_reporter_, attribute, decl);
  }
}

SourceLocation Library::GeneratedSimpleName(const std::string& name) {
  return generated_source_file_.AddLine(name);
}

Name Library::NextAnonymousName() {
  // TODO(FIDL-596): Improve anonymous name generation. We want to be
  // specific about how these names are generated once they appear in the
  // JSON IR, and are exposed to the backends.
  std::ostringstream data;
  data << "SomeLongAnonymousPrefix";
  data << anon_counter_++;
  return Name(this, GeneratedSimpleName(data.str()));
}

Name Library::DerivedName(const std::vector<std::string_view>& components) {
  return Name(this, GeneratedSimpleName(StringJoin(components, "_")));
}

std::optional<Name> Library::CompileCompoundIdentifier(
    const raw::CompoundIdentifier* compound_identifier) {
  const auto& components = compound_identifier->components;
  assert(components.size() >= 1);

  SourceLocation decl_name = components.back()->location();

  // First try resolving the identifier in the library.
  if (components.size() == 1) {
    return Name(this, decl_name);
  }

  std::vector<std::string_view> library_name;
  for (auto iter = components.begin(); iter != components.end() - 1; ++iter) {
    library_name.push_back((*iter)->location().data());
  }

  auto filename = compound_identifier->location().source_file().filename();
  Library* dep_library = nullptr;
  if (dependencies_.LookupAndUse(filename, library_name, &dep_library)) {
    return Name(dep_library, decl_name);
  }

  // If the identifier is not found in the library it might refer to a
  // declaration with a member (e.g. library.EnumX.val or BitsY.val).
  SourceLocation member_name = decl_name;
  SourceLocation member_decl_name = components.rbegin()[1]->location();

  if (components.size() == 2) {
    return Name(this, member_decl_name, std::string(member_name.data()));
  }

  std::vector<std::string_view> member_library_name(library_name);
  member_library_name.pop_back();

  Library* member_dep_library = nullptr;
  if (dependencies_.LookupAndUse(filename, member_library_name, &member_dep_library)) {
    return Name(member_dep_library, member_decl_name, std::string(member_name.data()));
  }

  std::string message("Unknown dependent library ");
  message += NameLibrary(library_name);
  message += " or reference to member of library ";
  message += NameLibrary(member_library_name);
  message += ". Did you require it with `using`?";
  const auto& location = components[0]->location();
  Fail(location, message);
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
    case Decl::Kind::kXUnion:
      StoreDecl(decl_ptr, &xunion_declarations_);
      break;
  }  // switch

  const Name* name = &decl_ptr->name;
  auto iter = declarations_.emplace(name, decl_ptr);
  if (!iter.second) {
    std::string message = "Name collision: ";
    message.append(name->name_part());
    return Fail(*name, message);
  }
  if (name->maybe_location()) {
    if (dependencies_.Contains(name->maybe_location()->source_file().filename(),
                               {name->maybe_location()->data()})) {
      std::string message = "Declaration name '";
      message.append(name->name_full());
      message.append(
          "' conflicts with a library import; consider using the "
          "'as' keyword to import the library under a different name.");
      return Fail(*name, message);
    }
  }

  switch (kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kService:
    case Decl::Kind::kStruct:
    case Decl::Kind::kTable:
    case Decl::Kind::kUnion:
    case Decl::Kind::kXUnion:
    case Decl::Kind::kProtocol: {
      auto type_decl = static_cast<TypeDecl*>(decl_ptr);
      auto type_template = std::make_unique<TypeDeclTypeTemplate>(
          Name(name->library(), std::string(name->name_part())), typespace_, error_reporter_, this,
          type_decl);
      typespace_->AddTemplate(std::move(type_template));
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<TypeAlias*>(decl_ptr);
      auto type_alias_template = std::make_unique<TypeAliasTypeTemplate>(
          Name(name->library(), std::string(name->name_part())), typespace_, error_reporter_, this,
          type_alias_decl);
      typespace_->AddTemplate(std::move(type_alias_template));
      break;
    }
    case Decl::Kind::kConst:
      break;
  }  // switch
  return true;
}

bool Library::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                              std::unique_ptr<Constant>* out_constant) {
  switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
      auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
      auto name = CompileCompoundIdentifier(identifier->identifier.get());
      if (!name)
        return false;
      *out_constant = std::make_unique<IdentifierConstant>(std::move(name.value()));
      break;
    }
    case raw::Constant::Kind::kLiteral: {
      auto literal = static_cast<raw::LiteralConstant*>(raw_constant.get());
      *out_constant = std::make_unique<LiteralConstant>(std::move(literal->literal));
      break;
    }
  }
  return true;
}

bool Library::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                     SourceLocation location,
                                     std::unique_ptr<TypeConstructor>* out_type_ctor) {
  auto name = CompileCompoundIdentifier(raw_type_ctor->identifier.get());
  if (!name)
    return false;

  std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
  if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
    if (!ConsumeTypeConstructor(std::move(raw_type_ctor->maybe_arg_type_ctor), location,
                                &maybe_arg_type_ctor))
      return false;
  }

  std::unique_ptr<Constant> maybe_size;
  if (raw_type_ctor->maybe_size != nullptr) {
    if (!ConsumeConstant(std::move(raw_type_ctor->maybe_size), location, &maybe_size))
      return false;
  }

  *out_type_ctor = std::make_unique<TypeConstructor>(
      std::move(name.value()), std::move(maybe_arg_type_ctor), raw_type_ctor->handle_subtype,
      std::move(maybe_size), raw_type_ctor->nullability);
  return true;
}

bool Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
  if (using_directive->maybe_type_ctor)
    return ConsumeTypeAlias(std::move(using_directive));

  if (using_directive->attributes && using_directive->attributes->attributes.size() != 0) {
    std::string attributes_found = "";
    for (const auto& attribute : using_directive->attributes->attributes) {
      if (attributes_found.size() != 0) {
        attributes_found.append(", ");
      }
      attributes_found.append(attribute.name);
    }
    std::string message("no attributes allowed on library import, found: ");
    message.append(attributes_found);
    const auto& location = using_directive->location();
    return Fail(location, message);
  }

  std::vector<std::string_view> library_name;
  for (const auto& component : using_directive->using_path->components) {
    library_name.push_back(component->location().data());
  }

  Library* dep_library = nullptr;
  if (!all_libraries_->Lookup(library_name, &dep_library)) {
    std::string message("Could not find library named ");
    message += NameLibrary(library_name);
    message += ". Did you include its sources with --files?";
    const auto& location = using_directive->using_path->components[0]->location();
    return Fail(location, message);
  }

  auto filename = using_directive->location().source_file().filename();
  if (!dependencies_.Register(using_directive->location(), filename, dep_library,
                              using_directive->maybe_alias)) {
    std::string message("Library ");
    message += NameLibrary(library_name);
    message += " already imported. Did you require it twice?";
    return Fail(message);
  }

  // Import declarations, and type aliases of dependent library.
  const auto& declarations = dep_library->declarations_;
  declarations_.insert(declarations.begin(), declarations.end());
  return true;
}

bool Library::ConsumeTypeAlias(std::unique_ptr<raw::Using> using_directive) {
  assert(using_directive->maybe_type_ctor);

  auto location = using_directive->using_path->components[0]->location();
  auto alias_name = Name(this, location);
  std::unique_ptr<TypeConstructor> partial_type_ctor_;
  if (!ConsumeTypeConstructor(std::move(using_directive->maybe_type_ctor), location,
                              &partial_type_ctor_))
    return false;
  return RegisterDecl(std::make_unique<TypeAlias>(std::move(using_directive->attributes),
                                                  std::move(alias_name),
                                                  std::move(partial_type_ctor_)));
}

bool Library::ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
  std::vector<Bits::Member> members;
  for (auto& member : bits_declaration->members) {
    auto location = member->identifier->location();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), location, &value))
      return false;
    members.emplace_back(location, std::move(value), std::move(member->attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the bits name qualifier such as
    // <name_of_bits>.<name_of_member>.
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  if (bits_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructor(std::move(bits_declaration->maybe_type_ctor),
                                bits_declaration->location(), &type_ctor))
      return false;
  } else {
    type_ctor = TypeConstructor::CreateSizeType();
  }

  return RegisterDecl(std::make_unique<Bits>(
      std::move(bits_declaration->attributes), Name(this, bits_declaration->identifier->location()),
      std::move(type_ctor), std::move(members), bits_declaration->strictness));
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
  auto attributes = std::move(const_declaration->attributes);
  auto location = const_declaration->identifier->location();
  auto name = Name(this, location);
  std::unique_ptr<TypeConstructor> type_ctor;
  if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), location, &type_ctor))
    return false;

  std::unique_ptr<Constant> constant;
  if (!ConsumeConstant(std::move(const_declaration->constant), location, &constant))
    return false;

  return RegisterDecl(std::make_unique<Const>(std::move(attributes), std::move(name),
                                              std::move(type_ctor), std::move(constant)));
}

bool Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
  std::vector<Enum::Member> members;
  for (auto& member : enum_declaration->members) {
    auto location = member->identifier->location();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), location, &value))
      return false;
    members.emplace_back(location, std::move(value), std::move(member->attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the enum name qualifier such as
    // <name_of_enum>.<name_of_member>.
  }

  std::unique_ptr<TypeConstructor> type_ctor;
  if (enum_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructor(std::move(enum_declaration->maybe_type_ctor),
                                enum_declaration->location(), &type_ctor))
      return false;
  } else {
    type_ctor = TypeConstructor::CreateSizeType();
  }

  return RegisterDecl(std::make_unique<Enum>(
      std::move(enum_declaration->attributes), Name(this, enum_declaration->identifier->location()),
      std::move(type_ctor), std::move(members), enum_declaration->strictness));
}

bool Library::CreateMethodResult(const Name& protocol_name, raw::ProtocolMethod* method,
                                 Struct* in_response, Struct** out_response) {
  // Compile the error type.
  auto error_location = method->maybe_error_ctor->location();
  std::unique_ptr<TypeConstructor> error_type_ctor;
  if (!ConsumeTypeConstructor(std::move(method->maybe_error_ctor), error_location,
                              &error_type_ctor))
    return false;

  // Make the Result union containing the response struct and the
  // error type.
  SourceLocation method_name = method->identifier->location();
  Name result_name = DerivedName({protocol_name.name_part(), method_name.data(), "Result"});
  raw::SourceElement sourceElement = raw::SourceElement(fidl::Token(), fidl::Token());
  Union::Member response_member{
      std::make_unique<raw::Ordinal32>(sourceElement, 1),  // success case explicitly has ordinal 1
      IdentifierTypeForDecl(in_response, types::Nullability::kNonnullable),
      GeneratedSimpleName("response"), nullptr};
  Union::Member error_member{
      std::make_unique<raw::Ordinal32>(sourceElement, 2),  // error case explicitly has ordinal 2
      std::move(error_type_ctor), GeneratedSimpleName("err"), nullptr};
  std::vector<Union::Member> result_members;
  result_members.push_back(std::move(response_member));
  result_members.push_back(std::move(error_member));
  std::vector<raw::Attribute> result_attributes;
  result_attributes.emplace_back(*method, "Result", "");
  auto result_attributelist =
      std::make_unique<raw::AttributeList>(*method, std::move(result_attributes));
  auto xunion_decl = std::make_unique<Union>(std::move(result_attributelist),
                                             std::move(result_name), std::move(result_members));
  auto result_decl = xunion_decl.get();
  if (!RegisterDecl(std::move(xunion_decl)))
    return false;

  // Make a new response struct for the method containing just the
  // result union.
  std::vector<Struct::Member> response_members;
  response_members.push_back(
      Struct::Member(IdentifierTypeForDecl(result_decl, types::Nullability::kNonnullable),
                     GeneratedSimpleName("result"), nullptr, nullptr));
  auto struct_decl = std::make_unique<Struct>(nullptr /* attributes */, NextAnonymousName(),
                                              std::move(response_members), true);
  auto struct_decl_ptr = struct_decl.get();
  if (!RegisterDecl(std::move(struct_decl)))
    return false;
  *out_response = struct_decl_ptr;
  return true;
}

bool Library::ConsumeProtocolDeclaration(
    std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration) {
  auto attributes = std::move(protocol_declaration->attributes);
  auto name = Name(this, protocol_declaration->identifier->location());

  std::set<Name> composed_protocols;
  for (auto& composed_protocol : protocol_declaration->composed_protocols) {
    auto& protocol_name = composed_protocol->protocol_name;
    auto composed_protocol_name = CompileCompoundIdentifier(protocol_name.get());
    if (!composed_protocol_name)
      return false;
    auto maybe_location = composed_protocol_name.value().maybe_location();
    if (!composed_protocols.insert(std::move(composed_protocol_name.value())).second)
      return Fail(maybe_location, "protocol composed multiple times");
  }

  std::vector<Protocol::Method> methods;
  for (auto& method : protocol_declaration->methods) {
    auto generated_ordinal32 = std::make_unique<raw::Ordinal32>(
        fidl::ordinals::GetGeneratedOrdinal32(library_name_, name.name_part(), *method));
    auto generated_ordinal64 = std::make_unique<raw::Ordinal64>(
        fidl::ordinals::GetGeneratedOrdinal64(library_name_, name.name_part(), *method));
    auto attributes = std::move(method->attributes);
    SourceLocation method_name = method->identifier->location();

    Struct* maybe_request = nullptr;
    if (method->maybe_request != nullptr) {
      Name request_name = NextAnonymousName();
      if (!ConsumeParameterList(std::move(request_name), std::move(method->maybe_request), true,
                                &maybe_request))
        return false;
    }

    bool has_error = (method->maybe_error_ctor != nullptr);

    Struct* maybe_response = nullptr;
    if (method->maybe_response != nullptr) {
      Name response_name = has_error
                               ? DerivedName({name.name_part(), method_name.data(), "Response"})
                               : NextAnonymousName();
      if (!ConsumeParameterList(std::move(response_name), std::move(method->maybe_response),
                                !has_error, &maybe_response))
        return false;
    }

    if (has_error) {
      if (!CreateMethodResult(name, method.get(), maybe_response, &maybe_response))
        return false;
    }

    assert(maybe_request != nullptr || maybe_response != nullptr);
    methods.emplace_back(std::move(attributes), std::move(generated_ordinal32),
                         std::move(generated_ordinal64), std::move(method_name),
                         std::move(maybe_request), std::move(maybe_response));
  }

  return RegisterDecl(std::make_unique<Protocol>(
      std::move(attributes), std::move(name), std::move(composed_protocols), std::move(methods)));
}

std::unique_ptr<TypeConstructor> Library::IdentifierTypeForDecl(const Decl* decl,
                                                                types::Nullability nullability) {
  return std::make_unique<TypeConstructor>(
      Name(decl->name.library(), std::string(decl->name.name_part())), nullptr /* maybe_arg_type */,
      std::optional<types::HandleSubtype>(), nullptr /* maybe_size */, nullability);
}

bool Library::ConsumeParameterList(Name name, std::unique_ptr<raw::ParameterList> parameter_list,
                                   bool is_request_or_response, Struct** out_struct_decl) {
  std::vector<Struct::Member> members;
  for (auto& parameter : parameter_list->parameter_list) {
    const SourceLocation name = parameter->identifier->location();
    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(parameter->type_ctor), name, &type_ctor))
      return false;
    members.emplace_back(std::move(type_ctor), name, nullptr /* maybe_default_value */,
                         nullptr /* attributes */);
  }

  if (!RegisterDecl(std::make_unique<Struct>(nullptr /* attributes */, std::move(name),
                                             std::move(members), is_request_or_response)))
    return false;
  *out_struct_decl = struct_declarations_.back().get();
  return true;
}

bool Library::ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl) {
  auto attributes = std::move(service_decl->attributes);
  auto name = Name(this, service_decl->identifier->location());

  std::vector<Service::Member> members;
  for (auto& member : service_decl->members) {
    std::unique_ptr<TypeConstructor> type_ctor;
    auto location = member->identifier->location();
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), location, &type_ctor))
      return false;
    members.emplace_back(std::move(type_ctor), member->identifier->location(),
                         std::move(member->attributes));
  }

  return RegisterDecl(
      std::make_unique<Service>(std::move(attributes), std::move(name), std::move(members)));
}

bool Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
  auto attributes = std::move(struct_declaration->attributes);
  auto name = Name(this, struct_declaration->identifier->location());

  std::vector<Struct::Member> members;
  for (auto& member : struct_declaration->members) {
    std::unique_ptr<TypeConstructor> type_ctor;
    auto location = member->identifier->location();
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), location, &type_ctor))
      return false;
    std::unique_ptr<Constant> maybe_default_value;
    if (member->maybe_default_value != nullptr) {
      if (!ConsumeConstant(std::move(member->maybe_default_value), location, &maybe_default_value))
        return false;
    }
    auto attributes = std::move(member->attributes);
    members.emplace_back(std::move(type_ctor), member->identifier->location(),
                         std::move(maybe_default_value), std::move(attributes));
  }

  return RegisterDecl(
      std::make_unique<Struct>(std::move(attributes), std::move(name), std::move(members)));
}

bool Library::ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration) {
  auto attributes = std::move(table_declaration->attributes);
  auto name = Name(this, table_declaration->identifier->location());

  std::vector<Table::Member> members;
  for (auto& member : table_declaration->members) {
    auto ordinal_literal = std::move(member->ordinal);

    if (member->maybe_used) {
      std::unique_ptr<TypeConstructor> type_ctor;
      if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), member->location(),
                                  &type_ctor))
        return false;
      std::unique_ptr<Constant> maybe_default_value;
      if (member->maybe_used->maybe_default_value) {
        // TODO(FIDL-609): Support defaults on tables.
        const auto default_value = member->maybe_used->maybe_default_value.get();
        error_reporter_->ReportError(default_value->location(),
                                     "Defaults on tables are not yet supported.");
      }
      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        return Fail(member->location(), "Table members cannot be nullable");
      }
      auto attributes = std::move(member->maybe_used->attributes);
      members.emplace_back(std::move(ordinal_literal), std::move(type_ctor),
                           member->maybe_used->identifier->location(),
                           std::move(maybe_default_value), std::move(attributes));
    } else {
      members.emplace_back(std::move(ordinal_literal), member->location());
    }
  }

  return RegisterDecl(std::make_unique<Table>(std::move(attributes), std::move(name),
                                              std::move(members), table_declaration->strictness));
}

bool Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
  auto name = Name(this, union_declaration->identifier->location());

  assert(!union_declaration->members.empty() && "unions must have at least one member");
  std::vector<Union::Member> members;
  for (auto& member : union_declaration->members) {
    auto xunion_ordinal = std::move(member->ordinal);

    if (member->maybe_used) {
      auto location = member->maybe_used->identifier->location();
      std::unique_ptr<TypeConstructor> type_ctor;
      if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), location, &type_ctor))
        return false;

      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        return Fail(member->location(), "Union members cannot be nullable");
      }

      auto attributes = std::move(member->maybe_used->attributes);
      members.emplace_back(std::move(xunion_ordinal), std::move(type_ctor), location,
                           std::move(attributes));
    } else {
      assert(xunion_ordinal && "Reserved union members must have an ordinal specified");
      members.emplace_back(std::move(xunion_ordinal), member->location());
    }
  }

  auto attributes = std::move(union_declaration->attributes);

  return RegisterDecl(std::make_unique<Union>(std::move(attributes), std::move(name),
                                              std::move(members)));
}

bool Library::ConsumeXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> xunion_declaration) {
  auto name = Name(this, xunion_declaration->identifier->location());

  assert(!xunion_declaration->members.empty() && "unions must have at least one member");
  bool are_ordinals_explicit = xunion_declaration->members[0]->maybe_ordinal != nullptr;
  std::vector<XUnion::Member> members;
  for (auto& member : xunion_declaration->members) {
    auto ordinal = std::move(member->maybe_ordinal);

    bool is_member_ordinal_explicit = ordinal != nullptr;
    if (are_ordinals_explicit != is_member_ordinal_explicit) {
      return Fail(member->location(), "cannot mix explicit and implicit ordinals");
    }

    if (member->maybe_used) {
      if (!ordinal) {
        ordinal = std::make_unique<raw::Ordinal32>(
            fidl::ordinals::GetGeneratedOrdinal32(library_name_, name.name_part(), *member));
      }
      auto location = member->maybe_used->identifier->location();
      std::unique_ptr<TypeConstructor> type_ctor;
      if (!ConsumeTypeConstructor(std::move(member->maybe_used->type_ctor), location, &type_ctor))
        return false;

      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        return Fail(member->location(), "Extensible union members cannot be nullable");
      }

      members.emplace_back(std::move(ordinal), std::move(type_ctor), location,
                           std::move(member->maybe_used->attributes));
    } else {
      assert(ordinal && "Reserved union members must have an ordinal specified");
      members.emplace_back(std::move(ordinal), member->location());
    }
  }

  return RegisterDecl(std::make_unique<XUnion>(
      std::move(xunion_declaration->attributes), std::move(name), std::move(members),
      xunion_declaration->strictness, are_ordinals_explicit));
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
  if (file->attributes) {
    ValidateAttributesPlacement(AttributeSchema::Placement::kLibrary, file->attributes.get());
    if (!attributes_) {
      attributes_ = std::move(file->attributes);
    } else {
      AttributesBuilder attributes_builder(error_reporter_, std::move(attributes_->attributes));
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
    new_name.push_back(part->location().data());
  }
  if (!library_name_.empty()) {
    if (new_name != library_name_) {
      return Fail(file->library_name->components[0]->location(),
                  "Two files in the library disagree about the name of the library");
    }
  } else {
    library_name_ = new_name;
  }

  auto using_list = std::move(file->using_list);
  for (auto& using_directive : using_list) {
    if (!ConsumeUsing(std::move(using_directive))) {
      return false;
    }
  }

  auto bits_declaration_list = std::move(file->bits_declaration_list);
  for (auto& bits_declaration : bits_declaration_list) {
    if (!ConsumeBitsDeclaration(std::move(bits_declaration))) {
      return false;
    }
  }

  auto const_declaration_list = std::move(file->const_declaration_list);
  for (auto& const_declaration : const_declaration_list) {
    if (!ConsumeConstDeclaration(std::move(const_declaration))) {
      return false;
    }
  }

  auto enum_declaration_list = std::move(file->enum_declaration_list);
  for (auto& enum_declaration : enum_declaration_list) {
    if (!ConsumeEnumDeclaration(std::move(enum_declaration))) {
      return false;
    }
  }

  auto protocol_declaration_list = std::move(file->protocol_declaration_list);
  for (auto& protocol_declaration : protocol_declaration_list) {
    if (!ConsumeProtocolDeclaration(std::move(protocol_declaration))) {
      return false;
    }
  }

  auto service_declaration_list = std::move(file->service_declaration_list);
  for (auto& service_declaration : service_declaration_list) {
    if (!ConsumeServiceDeclaration(std::move(service_declaration))) {
      return false;
    }
  }

  auto struct_declaration_list = std::move(file->struct_declaration_list);
  for (auto& struct_declaration : struct_declaration_list) {
    if (!ConsumeStructDeclaration(std::move(struct_declaration))) {
      return false;
    }
  }

  auto table_declaration_list = std::move(file->table_declaration_list);
  for (auto& table_declaration : table_declaration_list) {
    if (!ConsumeTableDeclaration(std::move(table_declaration))) {
      return false;
    }
  }

  auto union_declaration_list = std::move(file->union_declaration_list);
  for (auto& union_declaration : union_declaration_list) {
    if (!ConsumeUnionDeclaration(std::move(union_declaration))) {
      return false;
    }
  }

  auto xunion_declaration_list = std::move(file->xunion_declaration_list);
  for (auto& xunion_declaration : xunion_declaration_list) {
    if (!ConsumeXUnionDeclaration(std::move(xunion_declaration))) {
      return false;
    }
  }

  return true;
}

bool Library::ResolveConstant(Constant* constant, const Type* type) {
  assert(constant != nullptr);

  if (constant->IsResolved())
    return true;

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
    }
  }

  __UNREACHABLE;
}

bool Library::ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type) {
  assert(TypeCanBeConst(type) &&
         "Compiler bug: resolving identifier constant to non-const-able type!");

  auto decl = LookupDeclByName(identifier_constant->name.memberless_name());
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
        break;
      }
      __FALLTHROUGH;
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
        break;
      }
      __FALLTHROUGH;
    }
    default: {
      std::ostringstream msg_stream;
      msg_stream << NameFlatConstant(identifier_constant) << " is a type, but a value was expected";
      return Fail(identifier_constant->name.maybe_location(), msg_stream.str());
    }
  }

  assert(const_val && "Compiler bug: did not set const_val");
  assert(const_type_ctor && "Compiler bug: did not set const_type_ctor");

  auto constant_kind = [](const types::PrimitiveSubtype primitive_subtype) {
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
  };

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
      if (!const_val->Convert(constant_kind(primitive_type->subtype), &resolved_val))
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
      if (decl->name != identifier_type->type_decl->name) {
        std::ostringstream msg;
        msg << "mismatched named type assignment: cannot define a constant or default value of "
               "type "
            << decl->name.name_full() << " using a value of type "
            << identifier_type->type_decl->name.name_full();
        return Fail(msg.str());
      }
      if (!const_val->Convert(constant_kind(primitive_type->subtype), &resolved_val))
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
  std::ostringstream msg_stream;
  msg_stream << NameFlatConstant(identifier_constant) << ", of type ";
  msg_stream << NameFlatTypeConstructor(const_type_ctor);
  msg_stream << ", cannot be converted to type " << NameFlatType(type);
  return Fail(msg_stream.str());
}

bool Library::ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type) {
  switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kString: {
      if (type->kind != Type::Kind::kString)
        goto return_fail;
      auto string_type = static_cast<const StringType*>(type);
      auto string_literal = static_cast<raw::StringLiteral*>(literal_constant->literal.get());
      auto string_data = string_literal->location().data();

      // TODO(pascallouis): because data() contains the raw content,
      // with the two " to identify strings, we need to take this
      // into account. We should expose the actual size of string
      // literals properly, and take into account escaping.
      uint64_t string_size = string_data.size() - 2;
      if (string_type->max_size->value < string_size) {
        std::ostringstream msg_stream;
        msg_stream << NameFlatConstant(literal_constant) << " (string:" << string_size;
        msg_stream << ") exceeds the size bound of type " << NameFlatType(type);
        return Fail(literal_constant->literal->location(), msg_stream.str());
      }

      literal_constant->ResolveTo(
          std::make_unique<StringConstantValue>(string_literal->location().data()));
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
      std::ostringstream msg_stream;
      msg_stream << NameFlatConstant(literal_constant) << " cannot be interpreted as type ";
      msg_stream << NameFlatType(type);
      return Fail(literal_constant->literal->location(), msg_stream.str());
    }
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

Decl* Library::LookupDeclByName(const Name& name) const {
  auto iter = declarations_.find(&name);
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

  auto data = literal->location().data();
  std::string string_data(data.data(), data.data() + data.size());
  auto result = utils::ParseNumeric(string_data, out_value);
  return result == utils::ParseNumericResult::kSuccess;
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
      if (name.name_part() == "request") {
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
  auto maybe_add_constant = [this, &edges](const Constant* constant) -> bool {
    switch (constant->kind) {
      case Constant::Kind::kIdentifier: {
        auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
        auto decl = LookupDeclByName(identifier->name.memberless_name());
        if (decl == nullptr) {
          std::string message("Unable to find the constant named: ");
          message += identifier->name.name_full();
          return Fail(identifier->name, message.data());
        }
        edges.insert(decl);
        break;
      }
      case Constant::Kind::kLiteral:
      case Constant::Kind::kSynthesized: {
        // Literal and synthesized constants have no dependencies on other declarations.
        break;
      }
    }
    return true;
  };
  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<const Bits*>(decl);
      maybe_add_decl(bits_decl->subtype_ctor.get());
      for (const auto& member : bits_decl->members) {
        if (!maybe_add_constant(member.value.get())) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      maybe_add_decl(const_decl->type_ctor.get());
      if (!maybe_add_constant(const_decl->value.get())) {
        return false;
      }
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<const Enum*>(decl);
      maybe_add_decl(enum_decl->subtype_ctor.get());
      for (const auto& member : enum_decl->members) {
        if (!maybe_add_constant(member.value.get())) {
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
          if (!maybe_add_constant(member.maybe_default_value.get())) {
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
          if (!maybe_add_constant(member.maybe_used->maybe_default_value.get())) {
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
    case Decl::Kind::kXUnion: {
      auto xunion_decl = static_cast<const XUnion*>(decl);
      for (const auto& member : xunion_decl->members) {
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
      return a->name.name_part() < b->name.name_part();
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
    return Fail("There is an includes-cycle in declarations");
  }

  return true;
}

bool Library::CompileDecl(Decl* decl) {
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
    case Decl::Kind::kXUnion: {
      auto xunion_decl = static_cast<XUnion*>(decl);
      if (!CompileXUnion(xunion_decl))
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

bool Library::VerifyDeclAttributes(Decl* decl) {
  assert(decl->compiled && "verification must happen after compilation of decls");
  auto placement_ok = error_reporter_->Checkpoint();
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
    case Decl::Kind::kXUnion: {
      auto xunion_declaration = static_cast<XUnion*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(AttributeSchema::Placement::kXUnionDecl,
                                  xunion_declaration->attributes.get());
      for (const auto& member : xunion_declaration->members) {
        if (!member.maybe_used)
          continue;
        ValidateAttributesPlacement(AttributeSchema::Placement::kXUnionMember,
                                    member.maybe_used->attributes.get());
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(xunion_declaration, xunion_declaration->attributes.get());
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
  return true;
}

bool Library::CompileBits(Bits* bits_declaration) {
  if (!CompileTypeConstructor(bits_declaration->subtype_ctor.get()))
    return false;

  if (bits_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    std::string message("bits may only be of unsigned integral primitive type, found ");
    message.append(NameFlatType(bits_declaration->subtype_ctor->type));
    return Fail(*bits_declaration, message);
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
      std::string message("bits may only be of unsigned integral primitive type, found ");
      message.append(NameFlatType(bits_declaration->subtype_ctor->type));
      return Fail(*bits_declaration, message);
  }

  return true;
}

bool Library::CompileConst(Const* const_declaration) {
  if (!CompileTypeConstructor(const_declaration->type_ctor.get()))
    return false;
  const auto* const_type = const_declaration->type_ctor.get()->type;
  if (!TypeCanBeConst(const_type)) {
    std::ostringstream msg_stream;
    msg_stream << "invalid constant type " << NameFlatType(const_type);
    return Fail(*const_declaration, msg_stream.str());
  }
  if (!ResolveConstant(const_declaration->value.get(), const_type))
    return Fail(*const_declaration, "unable to resolve constant value");

  return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
  if (!CompileTypeConstructor(enum_declaration->subtype_ctor.get()))
    return false;

  if (enum_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
    std::string message("enums may only be of integral primitive type, found ");
    message.append(NameFlatType(enum_declaration->subtype_ctor->type));
    return Fail(*enum_declaration, message);
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(enum_declaration->subtype_ctor->type);
  enum_declaration->type = primitive_type;
  switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kInt8:
      if (!ValidateEnumMembers<int8_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kInt16:
      if (!ValidateEnumMembers<int16_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kInt32:
      if (!ValidateEnumMembers<int32_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kInt64:
      if (!ValidateEnumMembers<int64_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kUint8:
      if (!ValidateEnumMembers<uint8_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kUint16:
      if (!ValidateEnumMembers<uint16_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kUint32:
      if (!ValidateEnumMembers<uint32_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kUint64:
      if (!ValidateEnumMembers<uint64_t>(enum_declaration))
        return false;
      break;
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
      std::string message("enums may only be of integral primitive type, found ");
      message.append(NameFlatType(enum_declaration->subtype_ctor->type));
      return Fail(*enum_declaration, message);
  }

  return true;
}

bool HasSimpleLayout(const Decl* decl) { return decl->GetAttribute("Layout") == "Simple"; }

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
        std::string message("unknown type ");
        message.append(name.name_full());
        return Fail(name, message);
      }
      if (decl->kind != Decl::Kind::kProtocol)
        return Fail(name, "This declaration is not a protocol");
      if (!decl->HasAttribute("FragileBase")) {
        std::string message = "protocol ";
        message += NameFlatName(name);
        message += " is not marked by [FragileBase] attribute, disallowing protocol ";
        message += NameFlatName(protocol_declaration->name);
        message += " from composing it";
        return Fail(name, message);
      }
      auto composed_protocol = static_cast<const Protocol*>(decl);
      auto maybe_location = composed_protocol->name.maybe_location();
      assert(maybe_location);
      if (method_scope.protocols.Insert(composed_protocol, *maybe_location).ok()) {
        if (!Visitor(composed_protocol, Visitor))
          return false;
      } else {
        // Otherwise we have already seen this protocol in
        // the inheritance graph.
      }
    }
    for (const auto& method : protocol->methods) {
      auto name_result = method_scope.names.Insert(method.name.data(), method.name);
      if (!name_result.ok())
        return Fail(method.name,
                    "Multiple methods with the same name in a protocol; last occurrence was at " +
                        name_result.previous_occurrence().position_str());
      auto ordinal_result =
          method_scope.ordinals.Insert(method.generated_ordinal32->value, method.name);
      if (method.generated_ordinal32->value == 0)
        return Fail(method.generated_ordinal32->location(), "Ordinal value 0 disallowed.");
      if (!ordinal_result.ok()) {
        std::string replacement_method(
            fidl::ordinals::GetSelector(method.attributes.get(), method.name));
        replacement_method.push_back('_');
        return Fail(method.generated_ordinal32->location(),
                    "Multiple methods with the same ordinal in a protocol; previous was at " +
                        ordinal_result.previous_occurrence().position_str() +
                        ". Consider using attribute " + "[Selector=\"" + replacement_method +
                        "\"] to change the " + "name used to calculate the ordinal.");
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
    auto CreateMessage = [&](Struct* message) -> bool {
      Scope<std::string_view> scope;
      for (auto& param : message->members) {
        if (!scope.Insert(param.name.data(), param.name).ok())
          return Fail(param.name, "Multiple parameters with the same name in a method");
        if (!CompileTypeConstructor(param.type_ctor.get()))
          return false;
      }
      return true;
    };
    if (method.maybe_request) {
      if (!CreateMessage(method.maybe_request))
        return false;
    }
    if (method.maybe_response) {
      if (!CreateMessage(method.maybe_response))
        return false;
    }
  }

  return true;
}

bool Library::CompileService(Service* service_decl) {
  Scope<std::string_view> scope;
  for (auto& member : service_decl->members) {
    auto name_result = scope.Insert(member.name.data(), member.name);
    if (!name_result.ok())
      return Fail(member.name, "multiple service members with the same name; previous was at " +
                                   name_result.previous_occurrence().position_str());
    if (!CompileTypeConstructor(member.type_ctor.get()))
      return false;
    if (member.type_ctor->type->kind != Type::Kind::kIdentifier)
      return Fail(member.name, "only protocol members are allowed");
    auto member_identifier_type = static_cast<const IdentifierType*>(member.type_ctor->type);
    if (member_identifier_type->type_decl->kind != Decl::Kind::kProtocol)
      return Fail(member.name, "only protocol members are allowed");
    if (member.type_ctor->nullability != types::Nullability::kNonnullable)
      return Fail(member.name, "service members cannot be nullable");
  }
  return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
  Scope<std::string_view> scope;
  for (auto& member : struct_declaration->members) {
    auto name_result = scope.Insert(member.name.data(), member.name);
    if (!name_result.ok())
      return Fail(member.name, "Multiple struct fields with the same name; previous was at " +
                                   name_result.previous_occurrence().position_str());

    if (!CompileTypeConstructor(member.type_ctor.get()))
      return false;
    if (member.maybe_default_value) {
      const auto* default_value_type = member.type_ctor.get()->type;
      if (!TypeCanBeConst(default_value_type)) {
        std::ostringstream msg_stream;
        msg_stream << "struct field  " << member.name.data() << " has an invalid default type"
                   << NameFlatType(default_value_type);
        return Fail(*struct_declaration, msg_stream.str());
      }
      if (!ResolveConstant(member.maybe_default_value.get(), default_value_type)) {
        return false;
      }
    }
  }

  return true;
}

bool Library::CompileTable(Table* table_declaration) {
  Scope<std::string_view> name_scope;
  Ordinal32Scope ordinal_scope;

  for (auto& member : table_declaration->members) {
    auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
    if (!ordinal_result.ok())
      return Fail(member.ordinal->location(),
                  "Multiple table fields with the same ordinal; previous was at " +
                      ordinal_result.previous_occurrence().position_str());
    if (member.maybe_used) {
      auto name_result = name_scope.Insert(member.maybe_used->name.data(), member.maybe_used->name);
      if (!name_result.ok())
        return Fail(member.maybe_used->name,
                    "Multiple table fields with the same name; previous was at " +
                        name_result.previous_occurrence().position_str());
      if (!CompileTypeConstructor(member.maybe_used->type_ctor.get()))
        return false;
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, location] = *ordinal_and_loc;
    std::ostringstream msg_stream;
    msg_stream << "missing ordinal " << ordinal;
    msg_stream << " (ordinals must be dense); consider marking it reserved";
    return Fail(location, msg_stream.str());
  }

  return true;
}

bool Library::CompileUnion(Union* union_declaration) {
  Scope<std::string_view> scope;
  Ordinal32Scope ordinal_scope;

  for (auto& member : union_declaration->members) {
    auto ordinal_result =
        ordinal_scope.Insert(member.xunion_ordinal->value, member.xunion_ordinal->location());
    if (!ordinal_result.ok())
      return Fail(member.xunion_ordinal->location(),
                  "Multiple union fields with the same ordinal; previous was at " +
                      ordinal_result.previous_occurrence().position_str());
    if (member.maybe_used) {
      auto name_result = scope.Insert(member.maybe_used->name.data(), member.maybe_used->name);
      if (!name_result.ok())
        return Fail(member.maybe_used->name,
                    "Multiple union members with the same name; previous was at " +
                        name_result.previous_occurrence().position_str());
      if (!CompileTypeConstructor(member.maybe_used->type_ctor.get()))
        return false;
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, location] = *ordinal_and_loc;
    std::ostringstream msg_stream;
    msg_stream << "missing ordinal " << ordinal;
    msg_stream << " (ordinals must be dense); consider marking it reserved";
    return Fail(location, msg_stream.str());
  }

  return true;
}

bool Library::CompileXUnion(XUnion* xunion_declaration) {
  Scope<std::string_view> scope;
  Ordinal32Scope ordinal_scope;

  for (auto& member : xunion_declaration->members) {
    auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
    if (!ordinal_result.ok())
      return Fail(member.ordinal->location(),
                  "Multiple xunion fields with the same ordinal; previous was at " +
                      ordinal_result.previous_occurrence().position_str());
    if (member.maybe_used) {
      auto name_result = scope.Insert(member.maybe_used->name.data(), member.maybe_used->name);
      if (!name_result.ok())
        return Fail(member.maybe_used->name,
                    "Multiple xunion members with the same name; previous was at " +
                        name_result.previous_occurrence().position_str());

      if (!CompileTypeConstructor(member.maybe_used->type_ctor.get()))
        return false;
    }
  }

  auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope);
  if (xunion_declaration->are_ordinals_explicit && ordinal_and_loc) {
    auto [ordinal, location] = *ordinal_and_loc;
    std::ostringstream msg_stream;
    msg_stream << "missing ordinal " << ordinal;
    msg_stream << " (ordinals must be dense); consider marking it reserved";
    return Fail(location, msg_stream.str());
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
    auto temporary_mode = error_reporter_->OverrideMode(ErrorReporter::ReportingMode::kDoNotReport);
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
  for (Decl* decl : declaration_order_) {
    if (!CompileDecl(decl))
      return false;
  }

  for (Decl* decl : declaration_order_) {
    if (!VerifyDeclAttributes(decl))
      return false;
  }

  if (!dependencies_.VerifyAllDependenciesWereUsed(*this, error_reporter_))
    return false;

  return error_reporter_->errors().size() == 0;
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

  if (!typespace_->Create(type_ctor->name, maybe_arg_type, type_ctor->handle_subtype, size,
                          type_ctor->nullability, &type_ctor->type, &type_ctor->from_type_alias))
    return false;

  return true;
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
      if (name.library() == this && name.name_part() == "MAX" && !name.member_name()) {
        size_constant->ResolveTo(std::make_unique<Size>(Size::Max()));
      }
    }
  }
  if (!size_constant->IsResolved()) {
    return Fail(type_ctor->name.maybe_location(), "unable to parse size bound");
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
  for (auto& member : decl->members) {
    assert(member.value != nullptr && "Compiler bug: member value is null!");

    if (!ResolveConstant(member.value.get(), decl->subtype_ctor->type)) {
      std::string failure_message = "unable to resolve ";
      failure_message += decl_type;
      failure_message += " member";
      return Fail(member.name, failure_message);
    }

    // Check that the member identifier hasn't been used yet
    std::string name = NameIdentifier(member.name);
    auto name_result = name_scope.Insert(name, member.name);
    if (!name_result.ok()) {
      std::ostringstream msg_stream;
      msg_stream << "name of member " << name;
      msg_stream << " conflicts with previously declared member in the ";
      msg_stream << decl_type << " " << decl->GetName();

      // We can log the error and then continue validating for other issues in the decl
      success = Fail(member.name, msg_stream.str());
    }

    MemberType value =
        static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    auto value_result = value_scope.Insert(value, member.name);
    if (!value_result.ok()) {
      std::ostringstream msg_stream;
      msg_stream << "value of member " << name;
      msg_stream << " conflicts with previously declared member ";
      msg_stream << NameIdentifier(value_result.previous_occurrence()) << " in the ";
      msg_stream << decl_type << " " << decl->GetName();

      // We can log the error and then continue validating other members for other bugs
      success = Fail(member.name, msg_stream.str());
    }

    std::string validation_failure;
    if (!validator(value, &validation_failure)) {
      success = Fail(member.name, validation_failure);
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
  auto validator = [&mask](MemberType member, std::string* out_error) {
    if (!IsPowerOfTwo(member)) {
      *out_error = "bits members must be powers of two";
      return false;
    }
    mask |= member;
    return true;
  };
  if (!ValidateMembers<Bits, MemberType>(bits_decl, validator)) {
    return false;
  }
  *out_mask = mask;
  return true;
}

template <typename MemberType>
bool Library::ValidateEnumMembers(Enum* enum_decl) {
  static_assert(std::is_integral<MemberType>::value && !std::is_same<MemberType, bool>::value,
                "Enum members must be an integral type!");
  // No additional validation is required for enums.
  auto validator = [](MemberType member, std::string* out_error) { return true; };
  return ValidateMembers<Enum, MemberType>(enum_decl, validator);
}

bool Library::HasAttribute(std::string_view name) const {
  if (!attributes_)
    return false;
  return attributes_->HasAttribute(std::string(name));
}

const std::set<Library*>& Library::dependencies() const { return dependencies_.dependencies(); }

std::unique_ptr<TypeConstructor> TypeConstructor::CreateSizeType() {
  return std::make_unique<TypeConstructor>(
      Name(nullptr, "uint32"), nullptr /* maybe_arg_type */, std::optional<types::HandleSubtype>(),
      nullptr /* maybe_size */, types::Nullability::kNonnullable);
}

}  // namespace flat
}  // namespace fidl
