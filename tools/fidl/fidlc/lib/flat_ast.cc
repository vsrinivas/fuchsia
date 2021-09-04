// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat_ast.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <utility>

#include "fidl/attributes.h"
#include "fidl/diagnostic_types.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "fidl/flat/name.h"
#include "fidl/flat/types.h"
#include "fidl/lexer.h"
#include "fidl/names.h"
#include "fidl/ordinals.h"
#include "fidl/raw_ast.h"
#include "fidl/types.h"
#include "fidl/utils.h"

namespace fidl {
namespace flat {

using namespace diagnostics;

namespace {

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

// A helper class to derive the resourceness of synthesized decls based on their
// members. If the given std::optional<types::Resourceness> is already set
// (meaning the decl is user-defined, not synthesized), this does nothing.
//
// Types added via AddType must already be compiled. In other words, there must
// not be cycles among the synthesized decls.
class DeriveResourceness {
 public:
  explicit DeriveResourceness(std::optional<types::Resourceness>* target)
      : target_(target), derive_(!target->has_value()), result_(types::Resourceness::kValue) {}

  ~DeriveResourceness() {
    if (derive_) {
      *target_ = result_;
    }
  }

  void AddType(const Type* type) {
    if (derive_ && result_ == types::Resourceness::kValue &&
        type->Resourceness() == types::Resourceness::kResource) {
      result_ = types::Resourceness::kResource;
    }
  }

 private:
  std::optional<types::Resourceness>* const target_;
  const bool derive_;
  types::Resourceness result_;
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

  const bool is_transitional = decl.HasAttribute("transitional");

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
    const bool has_unknown = member->attributes && member->attributes->HasAttribute("unknown");
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

TypeConstructorPtr GetTypeCtorAsPtr(const TypeConstructor& type_ctor) {
  return std::visit(fidl::utils::matchers{
                        [](const std::unique_ptr<TypeConstructorOld>& e) -> TypeConstructorPtr {
                          return e.get();
                        },
                        [](const std::unique_ptr<TypeConstructorNew>& e) -> TypeConstructorPtr {
                          return e.get();
                        },
                    },
                    type_ctor);
}

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

bool Attribute::HasArg(std::string_view arg_name) const { return GetArg(arg_name).has_value(); }

MaybeAttributeArg Attribute::GetArg(std::string_view arg_name) const {
  std::string name = utils::canonicalize(arg_name);
  for (const auto& arg : args) {
    if (arg->name == name) {
      return *arg;
    }
  }
  return std::nullopt;
}

bool Attribute::HasStandaloneAnonymousArg() const {
  return GetStandaloneAnonymousArg().has_value();
}

MaybeAttributeArg Attribute::GetStandaloneAnonymousArg() const {
  assert(!resolved &&
         "if calling after attribute compilation, use GetArg(...) with the resolved name instead");
  MaybeAttributeArg anon_arg;
  [[maybe_unused]] size_t named_args = 0;
  for (const auto& arg : args) {
    if (!arg->name.has_value()) {
      assert(!anon_arg.has_value() && "multiple anonymous arguments is a parser error");
      anon_arg = *arg;
    } else {
      named_args += 1;
    }
  }

  assert(!(anon_arg.has_value() && named_args > 0) &&
         "an attribute with both anonymous and named arguments is a parser error");
  return anon_arg;
};

bool AttributeList::HasAttribute(std::string_view attribute_name) const {
  for (const auto& attribute : attributes) {
    // TODO(fxbug.dev/70247): once the migration is complete, we no longer
    //  need to do the the casting to lower_snake_case, so this check should
    //  be removed.
    if (attribute->name == attribute_name ||
        fidl::utils::to_lower_snake_case(attribute->name) == attribute_name)
      return true;
  }
  return false;
}

MaybeAttribute AttributeList::GetAttribute(std::string_view attribute_name) const {
  for (const auto& attribute : attributes) {
    // TODO(fxbug.dev/70247): once the migration is complete, we no longer
    //  need to do the the casting to lower_snake_case, so this check should
    //  be removed.
    if (attribute->name == attribute_name ||
        fidl::utils::to_lower_snake_case(attribute->name) == attribute_name)
      return *attribute;
  }
  return std::nullopt;
}

bool AttributeList::HasAttributeArg(std::string_view attribute_name,
                                    std::string_view arg_name) const {
  auto attribute = GetAttribute(attribute_name);
  if (!attribute)
    return false;
  return attribute.value().get().HasArg(arg_name);
}

MaybeAttributeArg AttributeList::GetAttributeArg(std::string_view attribute_name,
                                                 std::string_view arg_name) const {
  auto attribute = GetAttribute(attribute_name);
  if (!attribute)
    return std::nullopt;
  return attribute.value().get().GetArg(arg_name);
}

bool Decl::HasAttribute(std::string_view attribute_name) const {
  if (!attributes)
    return false;
  return attributes->HasAttribute(attribute_name);
}

MaybeAttribute Decl::GetAttribute(std::string_view attribute_name) const {
  if (!attributes)
    return std::nullopt;
  return attributes->GetAttribute(attribute_name);
}

bool Decl::HasAttributeArg(std::string_view attribute_name, std::string_view arg_name) const {
  if (!attributes)
    return false;
  return attributes->HasAttributeArg(attribute_name, arg_name);
}

MaybeAttributeArg Decl::GetAttributeArg(std::string_view attribute_name,
                                        std::string_view arg_name) const {
  if (!attributes)
    return std::nullopt;
  return attributes->GetAttributeArg(attribute_name, arg_name);
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
        case Type::Kind::kTransportSide:
        case Type::Kind::kPrimitive:
          return true;
        case Type::Kind::kArray:
        case Type::Kind::kVector:
        case Type::Kind::kString:
        case Type::Kind::kIdentifier:
        case Type::Kind::kBox:
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
    case Type::Kind::kTransportSide:
    case Type::Kind::kPrimitive:
      return depth == 0u;
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      if (identifier_type->type_decl->kind == Decl::Kind::kUnion) {
        auto union_name = std::make_pair<const std::string&, const std::string_view&>(
            LibraryName(identifier_type->name.library(), "."), identifier_type->name.decl_name());
        if (allowed_simple_unions.find(union_name) == allowed_simple_unions.end()) {
          // Any unions not in the allow-list are treated as non-simple.
          reporter->Report(ErrUnionCannotBeSimple, identifier_type->name.span(),
                           identifier_type->name);
          return false;
        }
      }
      // TODO(fxbug.dev/70247): This only applies to nullable structs, which goes
      // through the kBox path in the new syntax. This can be removed along with
      // old syntax support
      switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
          // If the identifier is nullable, then we can handle a depth of 1
          // because the secondary object is directly accessible.
          return depth <= 1u;
        case types::Nullability::kNonnullable:
          return depth == 0u;
      }
    }
    case Type::Kind::kBox: {
      // we can handle a depth of 1 because the secondary object is directly accessible.
      return depth <= 1u;
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

bool Typespace::Create(const LibraryMediator& lib, const flat::Name& name,
                       const std::unique_ptr<TypeConstructorOld>& maybe_arg_type_ctor,
                       const std::optional<Name>& handle_subtype_identifier,
                       const std::unique_ptr<Constant>& handle_rights,
                       const std::unique_ptr<Constant>& maybe_size, types::Nullability nullability,
                       const Type** out_type, LayoutInvocation* out_params) {
  std::unique_ptr<Type> type;
  if (!CreateNotOwned(lib, name, maybe_arg_type_ctor, handle_subtype_identifier, handle_rights,
                      maybe_size, nullability, &type, out_params))
    return false;
  types_.push_back(std::move(type));
  *out_type = types_.back().get();
  return true;
}

bool Typespace::Create(const LibraryMediator& lib, const flat::Name& name,
                       const std::unique_ptr<LayoutParameterList>& parameters,
                       const std::unique_ptr<TypeConstraints>& constraints, const Type** out_type,
                       LayoutInvocation* out_params) {
  std::unique_ptr<Type> type;
  if (!CreateNotOwned(lib, name, parameters, constraints, &type, out_params))
    return false;
  types_.push_back(std::move(type));
  *out_type = types_.back().get();
  return true;
}

bool Typespace::CreateNotOwned(const LibraryMediator& lib, const flat::Name& name,
                               const std::unique_ptr<TypeConstructorOld>& maybe_arg_type_ctor,
                               const std::optional<Name>& handle_subtype_identifier,
                               const std::unique_ptr<Constant>& handle_rights,
                               const std::unique_ptr<Constant>& maybe_size,
                               types::Nullability nullability, std::unique_ptr<Type>* out_type,
                               LayoutInvocation* out_params) {
  // TODO(pascallouis): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name,
  // arg_type, size, and nullability.

  auto type_template = LookupTemplate(name, fidl::utils::Syntax::kOld);
  if (type_template == nullptr) {
    reporter_->Report(ErrUnknownType, name.span(), name);
    return false;
  }
  if (type_template->HasGeneratedName() && (name.as_anonymous() == nullptr)) {
    reporter_->Report(ErrAnonymousNameReference, name.span(), name);
    return false;
  }
  return type_template->Create(lib,
                               {.name = name,
                                .maybe_arg_type_ctor = maybe_arg_type_ctor,
                                .handle_subtype_identifier = handle_subtype_identifier,
                                .handle_rights = handle_rights,
                                .maybe_size = maybe_size,
                                .nullability = nullability},
                               out_type, out_params);
}

bool Typespace::CreateNotOwned(const LibraryMediator& lib, const flat::Name& name,
                               const std::unique_ptr<LayoutParameterList>& parameters,
                               const std::unique_ptr<TypeConstraints>& constraints,
                               std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) {
  // TODO(pascallouis): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name,
  // arg_type, size, and nullability.

  auto type_template = LookupTemplate(name, fidl::utils::Syntax::kNew);
  if (type_template == nullptr) {
    reporter_->Report(ErrUnknownType, name.span(), name);
    return false;
  }
  if (type_template->HasGeneratedName() && (name.as_anonymous() == nullptr)) {
    reporter_->Report(ErrAnonymousNameReference, name.span(), name);
    return false;
  }
  return type_template->Create(lib,
                               {.name = name, .parameters = parameters, .constraints = constraints},
                               out_type, out_params);
}

template <typename T>
void Typespace::AddTemplate(std::unique_ptr<T> type_template) {
  old_syntax_templates_.emplace(type_template->name(), std::make_unique<T>(*type_template));
  new_syntax_templates_.emplace(type_template->name(), std::move(type_template));
}

const TypeTemplate* Typespace::LookupTemplate(const flat::Name& name,
                                              fidl::utils::Syntax syntax) const {
  const auto& typemap =
      syntax == fidl::utils::Syntax::kNew ? new_syntax_templates_ : old_syntax_templates_;
  auto global_name = Name::Key(nullptr, name.decl_name());
  if (auto iter = typemap.find(global_name); iter != typemap.end()) {
    return iter->second.get();
  }

  if (auto iter = typemap.find(name); iter != typemap.end()) {
    return iter->second.get();
  }

  return nullptr;
}

template <typename... Args>
bool TypeTemplate::Fail(const ErrorDef<const TypeTemplate*, Args...>& err,
                        const std::optional<SourceSpan>& span, const Args&... args) const {
  reporter_->Report(err, span, this, args...);
  return false;
}

template <typename... Args>
bool TypeTemplate::Fail(const ErrorDef<Args...>& err, const Args&... args) const {
  reporter_->Report(err, args...);
  return false;
}

bool TypeTemplate::ResolveOldSyntaxArgs(const LibraryMediator& lib,
                                        const OldSyntaxParamsAndConstraints& unresolved_args,
                                        std::unique_ptr<CreateInvocation>* out_args,
                                        LayoutInvocation* out_params) const {
  const Type* maybe_arg_type = nullptr;
  if (unresolved_args.maybe_arg_type_ctor != nullptr) {
    if (!lib.ResolveType(unresolved_args.maybe_arg_type_ctor.get()))
      return false;
    maybe_arg_type = unresolved_args.maybe_arg_type_ctor->type;
    out_params->element_type_resolved = maybe_arg_type;
    out_params->element_type_raw = unresolved_args.maybe_arg_type_ctor.get();
  }

  const Size* size = nullptr;
  if (unresolved_args.maybe_size != nullptr) {
    if (!lib.ResolveSizeBound(unresolved_args.maybe_size.get(), &size)) {
      reporter_->Report(ErrCouldNotParseSizeBound, unresolved_args.maybe_size->span);
      return false;
    }
    out_params->size_resolved = size;
    out_params->size_raw = unresolved_args.maybe_size.get();
  }

  Resource* handle_resource_decl = nullptr;
  if (unresolved_args.handle_subtype_identifier || unresolved_args.handle_rights) {
    if (!GetResource(lib, unresolved_args.name, &handle_resource_decl))
      return false;
    assert(handle_resource_decl);
  }

  std::optional<uint32_t> obj_type = std::nullopt;
  std::optional<types::HandleSubtype> handle_subtype = std::nullopt;
  if (unresolved_args.handle_subtype_identifier) {
    const Name& name = *unresolved_args.handle_subtype_identifier;
    // the new path uses Constants, the old path uses Names; convert the Name
    // to a Constant here to share code paths.
    auto into_constant = static_cast<std::unique_ptr<Constant>>(
        std::make_unique<IdentifierConstant>(name, *name.span()));
    assert(handle_resource_decl);
    uint32_t raw_obj_type;
    if (!lib.ResolveAsHandleSubtype(handle_resource_decl, into_constant, &raw_obj_type))
      return Fail(ErrCouldNotResolveHandleSubtype, name);
    obj_type = raw_obj_type;
    handle_subtype = types::HandleSubtype(raw_obj_type);
    out_params->subtype_resolved = raw_obj_type;
  }

  const HandleRights* rights = nullptr;
  if (unresolved_args.handle_rights) {
    if (!lib.ResolveAsHandleRights(handle_resource_decl, unresolved_args.handle_rights.get(),
                                   &rights))
      return Fail(ErrCouldNotResolveHandleRights);
    out_params->rights_resolved = rights;
    out_params->rights_raw = unresolved_args.handle_rights.get();
  }

  // No work needed for nullability - in the old syntax there's nothing to resolve
  // because ? always indicates nullable.
  out_params->nullability = unresolved_args.nullability;

  *out_args =
      std::make_unique<CreateInvocation>(unresolved_args.name, maybe_arg_type, obj_type,
                                         handle_subtype, rights, size, unresolved_args.nullability);
  return true;
}

bool TypeTemplate::GetResource(const LibraryMediator& lib, const Name& name,
                               Resource** out_resource) const {
  assert(false &&
         "Only the HandleTypeTemplate should ever need to do this, because of hardcoding in the "
         "parser");
  __builtin_unreachable();
}

bool TypeTemplate::HasGeneratedName() const { return name_.as_anonymous() != nullptr; }

class PrimitiveTypeTemplate : public TypeTemplate {
 public:
  PrimitiveTypeTemplate(Typespace* typespace, Reporter* reporter, const std::string& name,
                        types::PrimitiveSubtype subtype)
      : TypeTemplate(Name::CreateIntrinsic(name), typespace, reporter), subtype_(subtype) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 0) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);
    }

    PrimitiveType type(name_, subtype_);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args->name.span());

    PrimitiveType type(name_, subtype_);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }

 private:
  const types::PrimitiveSubtype subtype_;
};

bool PrimitiveType::ApplyConstraints(const flat::LibraryMediator& lib,
                                     const TypeConstraints& constraints, const TypeTemplate* layout,
                                     std::unique_ptr<Type>* out_type,
                                     LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TOOD(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1)
    return lib.Fail(ErrCannotBeNullable, constraints.items[0]->span, layout);
  if (num_constraints > 1)
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(0), num_constraints);
  *out_type = std::make_unique<PrimitiveType>(name, subtype);
  return true;
}

bool PrimitiveType::ApplySomeLayoutParametersAndConstraints(
    const LibraryMediator& lib, const CreateInvocation& create_invocation,
    const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
    LayoutInvocation* out_params) const {
  if (create_invocation.size != nullptr)
    return lib.Fail(ErrCannotHaveSize, create_invocation.name.span(), layout);
  if (create_invocation.nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotBeNullable, create_invocation.name.span(), layout);
  *out_type = std::make_unique<PrimitiveType>(name, subtype);
  return true;
}

class ArrayTypeTemplate final : public TypeTemplate {
 public:
  ArrayTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("array"), typespace, reporter) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    size_t expected_params = 2;
    if (num_params != expected_params) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span,
                  expected_params, num_params);
    }

    const Type* element_type = nullptr;
    if (!lib.ResolveParamAsType(this, unresolved_args.parameters->items[0], &element_type))
      return false;
    out_params->element_type_resolved = element_type;
    out_params->element_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

    const Size* size = nullptr;
    if (!lib.ResolveParamAsSize(this, unresolved_args.parameters->items[1], &size))
      return false;
    out_params->size_resolved = size;
    out_params->size_raw = unresolved_args.parameters->items[1]->AsConstant();

    ArrayType type(name_, element_type, size);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args->name.span());
    if (args->size == nullptr)
      return Fail(ErrMustHaveSize, args->name.span());
    if (args->size->value == 0)
      return Fail(ErrMustHaveNonZeroSize, args->name.span());

    ArrayType type(name_, args->arg_type, args->size);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }
};

bool ArrayType::ApplyConstraints(const flat::LibraryMediator& lib,
                                 const TypeConstraints& constraints, const TypeTemplate* layout,
                                 std::unique_ptr<Type>* out_type,
                                 LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TOOD(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1)
    return lib.Fail(ErrCannotBeNullable, constraints.items[0]->span, layout);
  if (num_constraints > 1)
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(0), num_constraints);
  *out_type = std::make_unique<ArrayType>(name, element_type, element_count);
  return true;
}

bool ArrayType::ApplySomeLayoutParametersAndConstraints(const LibraryMediator& lib,
                                                        const CreateInvocation& create_invocation,
                                                        const TypeTemplate* layout,
                                                        std::unique_ptr<Type>* out_type,
                                                        LayoutInvocation* out_params) const {
  if (create_invocation.size && create_invocation.size != element_count)
    return lib.Fail(ErrCannotParameterizeAlias, create_invocation.name.span(), layout);
  if (create_invocation.nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotBeNullable, create_invocation.name.span(), layout);
  *out_type = std::make_unique<ArrayType>(name, element_type, element_count);
  return true;
}

class BytesTypeTemplate final : public TypeTemplate {
 public:
  BytesTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter),
        uint8_type_(kUint8Type) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 0) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);
    }

    VectorType type(name_, &uint8_type_);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args->name.span());

    VectorType type(name_, &uint8_type_);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }

 private:
  // TODO(fxbug.dev/7724): Remove when canonicalizing types.
  const Name kUint8TypeName = Name::CreateIntrinsic("uint8");
  const PrimitiveType kUint8Type = PrimitiveType(kUint8TypeName, types::PrimitiveSubtype::kUint8);

  const PrimitiveType uint8_type_;
};

bool VectorBaseType::ResolveSizeAndNullability(const LibraryMediator& lib,
                                               const TypeConstraints& constraints,
                                               const TypeTemplate* layout,
                                               LayoutInvocation* out_params) {
  size_t num_constraints = constraints.items.size();
  if (num_constraints == 1) {
    LibraryMediator::ResolvedConstraint resolved;
    if (!lib.ResolveConstraintAs(
            constraints.items[0],
            {LibraryMediator::ConstraintKind::kSize, LibraryMediator::ConstraintKind::kNullability},
            nullptr /* resource_decl */, &resolved))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[0]->span, layout);
    switch (resolved.kind) {
      case LibraryMediator::ConstraintKind::kSize:
        out_params->size_resolved = resolved.value.size;
        out_params->size_raw = constraints.items[0].get();
        break;
      case LibraryMediator::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        break;
      default:
        assert(false && "Compiler bug: resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // first constraint must be size, followed by optional
    if (!lib.ResolveSizeBound(constraints.items[0].get(), &out_params->size_resolved))
      return lib.Fail(ErrCouldNotParseSizeBound, std::nullopt);
    out_params->size_raw = constraints.items[0].get();
    if (!lib.ResolveAsOptional(constraints.items[1].get())) {
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[1]->span, layout);
    }
    out_params->nullability = types::Nullability::kNullable;
  } else if (num_constraints >= 3) {
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(2), num_constraints);
  }
  return true;
}

const Size VectorBaseType::kMaxSize = Size::Max();

class VectorTypeTemplate final : public TypeTemplate {
 public:
  VectorTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("vector"), typespace, reporter) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 1) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(1),
                  num_params);
    }

    const Type* element_type = nullptr;
    if (!lib.ResolveParamAsType(this, unresolved_args.parameters->items[0], &element_type))
      return false;
    out_params->element_type_resolved = element_type;
    out_params->element_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

    VectorType type(name_, element_type);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args->name.span());
    VectorType type(name_, args->arg_type);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }
};

bool VectorType::ApplyConstraints(const flat::LibraryMediator& lib,
                                  const TypeConstraints& constraints, const TypeTemplate* layout,
                                  std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  if (!ResolveSizeAndNullability(lib, constraints, layout, out_params))
    return false;

  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = out_params->nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  if (element_count != &kMaxSize && out_params->size_resolved)
    return lib.Fail(ErrCannotBoundTwice, std::nullopt, layout);
  auto merged_size = out_params->size_resolved ? out_params->size_resolved : element_count;

  *out_type = std::make_unique<VectorType>(name, element_type, merged_size, merged_nullability);
  return true;
}

bool VectorType::ApplySomeLayoutParametersAndConstraints(const LibraryMediator& lib,
                                                         const CreateInvocation& create_invocation,
                                                         const TypeTemplate* layout,
                                                         std::unique_ptr<Type>* out_type,
                                                         LayoutInvocation* out_params) const {
  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = create_invocation.nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, create_invocation.name.span(), layout);
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  // TODO(fxbug.dev/74193): take the smaller bound
  if (element_count != &kMaxSize && create_invocation.size) {
    return lib.Fail(ErrCannotBoundTwice, std::nullopt, layout);
  }
  auto merged_size = create_invocation.size ? create_invocation.size : element_count;

  *out_type = std::make_unique<VectorType>(name, element_type, merged_size, merged_nullability);
  return true;
}

class StringTypeTemplate final : public TypeTemplate {
 public:
  StringTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("string"), typespace, reporter) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 0) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);
    }

    StringType type(name_);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args->name.span());

    StringType type(name_);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }
};

bool StringType::ApplyConstraints(const flat::LibraryMediator& lib,
                                  const TypeConstraints& constraints, const TypeTemplate* layout,
                                  std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  if (!ResolveSizeAndNullability(lib, constraints, layout, out_params))
    return false;

  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = out_params->nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  if (max_size != &kMaxSize && out_params->size_resolved)
    return lib.Fail(ErrCannotBoundTwice, std::nullopt, layout);
  auto merged_size = out_params->size_resolved ? out_params->size_resolved : max_size;

  *out_type = std::make_unique<StringType>(name, merged_size, merged_nullability);
  return true;
}

bool StringType::ApplySomeLayoutParametersAndConstraints(const LibraryMediator& lib,
                                                         const CreateInvocation& create_invocation,
                                                         const TypeTemplate* layout,
                                                         std::unique_ptr<Type>* out_type,
                                                         LayoutInvocation* out_params) const {
  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = create_invocation.nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, create_invocation.name.span(), layout);
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  // Note that we don't have a way of knowing whether a size was actually specified,
  // since unspecified sizes are always replaced with a MAX default. Assume that
  // MAX means unspecified (this means that we would allow bounding twice if the
  // user uses MAX both times).
  // TODO(fxbug.dev/74193): take the smaller bound
  if (*max_size != kMaxSize && create_invocation.size)
    return lib.Fail(ErrCannotBoundTwice, std::nullopt, layout);
  auto merged_size = create_invocation.size ? create_invocation.size : max_size;

  *out_type = std::make_unique<StringType>(name, merged_size, merged_nullability);
  return true;
}

class HandleTypeTemplate final : public TypeTemplate {
 public:
  HandleTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("handle"), typespace, reporter) {}

  // Currently we take a name as parameter, but the parser restricts this name to be
  // something that ends in "handle".
  // In a more general implementation, we would add such an entry at "Consume" time of
  // the resource in question, allowing us to set a pointer to the Resource declaration
  // on the HandleTypeTemplate itself. We can't currently do this because we don't have
  // access to the definition of "handle" when we insert it into the root typespace, so we
  // need to resort to looking it up and doing validation at runtime.
  bool GetResource(const LibraryMediator& lib, const Name& name,
                   Resource** out_resource) const override {
    Decl* handle_decl = lib.LookupDeclByName(name);
    if (!handle_decl || handle_decl->kind != Decl::Kind::kResource) {
      return Fail(ErrHandleNotResource, name);
    }

    auto* resource = static_cast<Resource*>(handle_decl);
    if (!IsTypeConstructorDefined(resource->subtype_ctor) ||
        GetName(resource->subtype_ctor).full_name() != "uint32") {
      reporter_->Report(ErrResourceMustBeUint32Derived, resource->name);
      return false;
    }

    *out_resource = resource;
    return true;
  }

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    Resource* handle_resource_decl = nullptr;
    if (!GetResource(lib, unresolved_args.name, &handle_resource_decl))
      return false;

    size_t num_params = !unresolved_args.parameters->items.empty();
    if (num_params) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);
    }

    HandleType type(name_, handle_resource_decl);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> resolved;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &resolved, out_params))
      return false;

    assert(resolved->arg_type == nullptr);

    if (resolved->size != nullptr)
      return Fail(ErrCannotHaveSize, resolved->name.span());

    // Note that in the old syntax, we'll already have looked up the Resource*
    // (if necessary) since the old syntax resolves arguments ahead of time (see
    // call to ResolveOldSyntaxArgs above). However, we still need to obtain the
    // Resource* and pass it to the HandleType, since it may be used to resolve
    // more constraints later (e.g. if there's an alias to this handle that also
    // specifies more constraints) in the new syntax.
    Resource* handle_resource_decl = nullptr;
    if (!GetResource(lib, unresolved_args.name, &handle_resource_decl))
      return false;

    HandleType type(name_, handle_resource_decl);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *resolved, this, out_type, out_params);
  }

 private:
  const static HandleRights kSameRights;
};

const HandleRights HandleType::kSameRights = HandleRights(kHandleSameRights);

bool HandleType::ApplyConstraints(const flat::LibraryMediator& lib,
                                  const TypeConstraints& constraints, const TypeTemplate* layout,
                                  std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  assert(resource_decl);

  // We need to store this separately from out_params, because out_params doesn't
  // store the raw Constant that gets resolved to a nullability constraint.
  std::optional<SourceSpan> applied_nullability_span;

  size_t num_constraints = constraints.items.size();
  if (num_constraints == 0) {
    // no constraints: set to default subtype below
  } else if (num_constraints == 1) {
    // lone constraint can be either subtype or optional
    auto constraint_span = constraints.items[0]->span;
    LibraryMediator::ResolvedConstraint resolved;
    if (!lib.ResolveConstraintAs(constraints.items[0],
                                 {LibraryMediator::ConstraintKind::kHandleSubtype,
                                  LibraryMediator::ConstraintKind::kNullability},
                                 resource_decl, &resolved))
      return lib.Fail(ErrUnexpectedConstraint, constraint_span, layout);
    switch (resolved.kind) {
      case LibraryMediator::ConstraintKind::kHandleSubtype:
        out_params->subtype_resolved = resolved.value.handle_subtype;
        out_params->subtype_raw = constraints.items[0].get();
        break;
      case LibraryMediator::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        assert(false && "Compiler bug: resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // the first constraint must be subtype
    auto constraint_span = constraints.items[0]->span;
    uint32_t obj_type = 0;
    if (!lib.ResolveAsHandleSubtype(resource_decl, constraints.items[0], &obj_type))
      return lib.Fail(ErrUnexpectedConstraint, constraint_span, layout);
    out_params->subtype_resolved = obj_type;
    out_params->subtype_raw = constraints.items[0].get();

    // the second constraint can either be rights or optional
    constraint_span = constraints.items[1]->span;
    LibraryMediator::ResolvedConstraint resolved;
    if (!lib.ResolveConstraintAs(constraints.items[1],
                                 {LibraryMediator::ConstraintKind::kHandleRights,
                                  LibraryMediator::ConstraintKind::kNullability},
                                 resource_decl, &resolved))
      return lib.Fail(ErrUnexpectedConstraint, constraint_span, layout);
    switch (resolved.kind) {
      case LibraryMediator::ConstraintKind::kHandleRights:
        out_params->rights_resolved = resolved.value.handle_rights;
        out_params->rights_raw = constraints.items[1].get();
        break;
      case LibraryMediator::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        assert(false && "Compiler bug: resolved to wrong constraint kind");
    }
  } else if (num_constraints == 3) {
    // no degrees of freedom: must be subtype, followed by rights, then optional
    uint32_t obj_type = 0;
    if (!lib.ResolveAsHandleSubtype(resource_decl, constraints.items[0], &obj_type))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[0]->span, layout);
    out_params->subtype_resolved = obj_type;
    out_params->subtype_raw = constraints.items[0].get();
    const HandleRights* rights = nullptr;
    if (!lib.ResolveAsHandleRights(resource_decl, constraints.items[1].get(), &rights))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[1]->span, layout);
    out_params->rights_resolved = rights;
    out_params->rights_raw = constraints.items[1].get();
    if (!lib.ResolveAsOptional(constraints.items[2].get()))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[2]->span, layout);
    out_params->nullability = types::Nullability::kNullable;
    applied_nullability_span = constraints.items[2]->span;
  } else {
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(3), num_constraints);
  }

  bool has_obj_type = subtype != types::HandleSubtype::kHandle;
  if (has_obj_type && out_params->subtype_resolved)
    return lib.Fail(ErrCannotConstrainTwice, out_params->subtype_raw->span, layout);
  // TODO(fxbug.dev/64629): We need to allow setting a default obj_type in
  // resource_definition declarations rather than hard-coding.
  uint32_t merged_obj_type = obj_type;
  if (out_params->subtype_resolved) {
    merged_obj_type = out_params->subtype_resolved.value();
  }

  bool has_nullability = nullability == types::Nullability::kNullable;
  if (has_nullability && out_params->nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, applied_nullability_span, layout);
  auto merged_nullability =
      has_nullability || out_params->nullability == types::Nullability::kNullable
          ? types::Nullability::kNullable
          : types::Nullability::kNonnullable;

  bool has_rights = rights != &kSameRights;
  if (has_rights && out_params->rights_resolved)
    return lib.Fail(ErrCannotConstrainTwice, out_params->rights_raw->span, layout);
  auto merged_rights = rights;
  if (out_params->rights_resolved) {
    merged_rights = out_params->rights_resolved;
  }

  *out_type = std::make_unique<HandleType>(name, resource_decl, merged_obj_type,
                                           types::HandleSubtype(merged_obj_type), merged_rights,
                                           merged_nullability);
  return true;
}

bool HandleType::ApplySomeLayoutParametersAndConstraints(const LibraryMediator& lib,
                                                         const CreateInvocation& create_invocation,
                                                         const TypeTemplate* layout,
                                                         std::unique_ptr<Type>* out_type,
                                                         LayoutInvocation* out_params) const {
  if (create_invocation.size)
    return lib.Fail(ErrCannotHaveSize, create_invocation.name.span(), layout);

  bool has_obj_type = subtype != types::HandleSubtype::kHandle;
  if (has_obj_type && create_invocation.obj_type)
    return lib.Fail(ErrCannotConstrainTwice, std::nullopt, layout);
  uint32_t merged_obj_type = obj_type;
  if (create_invocation.obj_type.has_value())
    merged_obj_type = create_invocation.obj_type.value();

  bool has_nullability = nullability == types::Nullability::kNullable;
  if (has_nullability && create_invocation.nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
  auto merged_nullability =
      has_nullability || create_invocation.nullability == types::Nullability::kNullable
          ? types::Nullability::kNullable
          : types::Nullability::kNonnullable;

  bool has_rights = rights != &kSameRights;
  if (has_rights && create_invocation.handle_rights)
    return lib.Fail(ErrCannotConstrainTwice, std::nullopt, layout);
  auto merged_rights = rights;
  if (create_invocation.handle_rights)
    merged_rights = create_invocation.handle_rights;

  *out_type = std::make_unique<HandleType>(name, resource_decl, merged_obj_type,
                                           types::HandleSubtype(merged_obj_type), merged_rights,
                                           merged_nullability);
  return true;
}

// TODO(fxbug.dev/70247): Remove this in favor of TransportSideTypeTemplate
class RequestTypeTemplate final : public TypeTemplate {
 public:
  RequestTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("request"), typespace, reporter) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    assert(false && "Compiler bug: this type template should only be used in the old syntax");
    return false;
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (args->arg_type == nullptr)
      return Fail(ErrMustBeParameterized, args->name.span());
    if (args->arg_type->kind != Type::Kind::kIdentifier)
      return Fail(ErrMustBeAProtocol, args->name.span());
    auto protocol_type = static_cast<const IdentifierType*>(args->arg_type);
    if (protocol_type->type_decl->kind != Decl::Kind::kProtocol)
      return Fail(ErrMustBeAProtocol, args->name.span());
    if (args->size != nullptr)
      return Fail(ErrCannotHaveSize, args->name.span());

    RequestHandleType type(name_, protocol_type);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }
};

bool RequestHandleType::ApplyConstraints(const flat::LibraryMediator& lib,
                                         const TypeConstraints& constraints,
                                         const TypeTemplate* layout,
                                         std::unique_ptr<Type>* out_type,
                                         LayoutInvocation* out_params) const {
  assert(false && "Compiler bug: this type should only be used in the old syntax");
  return false;
}

bool RequestHandleType::ApplySomeLayoutParametersAndConstraints(
    const LibraryMediator& lib, const CreateInvocation& create_invocation,
    const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
    LayoutInvocation* out_params) const {
  if (create_invocation.size)
    return lib.Fail(ErrCannotHaveSize, create_invocation.name.span(), layout);

  if (nullability == types::Nullability::kNullable &&
      create_invocation.nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
  auto merged_nullability = nullability;
  if (create_invocation.nullability == types::Nullability::kNullable)
    merged_nullability = create_invocation.nullability;

  *out_type = std::make_unique<RequestHandleType>(name, protocol_type, merged_nullability);
  return true;
}

class TransportSideTypeTemplate final : public TypeTemplate {
 public:
  TransportSideTypeTemplate(Typespace* typespace, Reporter* reporter, TransportSide end)
      : TypeTemplate(end == TransportSide::kClient ? Name::CreateIntrinsic("client_end")
                                                   : Name::CreateIntrinsic("server_end"),
                     typespace, reporter),
        end_(end) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = !unresolved_args.parameters->items.empty();
    if (num_params)
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);

    TransportSideType type(name_, end_);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    assert(false && "Compiler bug: this type template should only be used in the new syntax");
    return false;
  }

 private:
  TransportSide end_;
};

bool TransportSideType::ApplyConstraints(const flat::LibraryMediator& lib,
                                         const TypeConstraints& constraints,
                                         const TypeTemplate* layout,
                                         std::unique_ptr<Type>* out_type,
                                         LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();

  // We need to store this separately from out_params, because out_params doesn't
  // store the raw Constant that gets resolved to a nullability constraint.
  std::optional<SourceSpan> applied_nullability_span;

  if (num_constraints == 1) {
    // could either be a protocol or optional
    auto constraint_span = constraints.items[0]->span;
    LibraryMediator::ResolvedConstraint resolved;
    if (!lib.ResolveConstraintAs(constraints.items[0],
                                 {LibraryMediator::ConstraintKind::kProtocol,
                                  LibraryMediator::ConstraintKind::kNullability},
                                 /* resource_decl */ nullptr, &resolved))
      return lib.Fail(ErrUnexpectedConstraint, constraint_span, layout);
    switch (resolved.kind) {
      case LibraryMediator::ConstraintKind::kProtocol:
        out_params->protocol_decl = resolved.value.protocol_decl;
        out_params->protocol_decl_raw = constraints.items[0].get();
        break;
      case LibraryMediator::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        assert(false && "Compiler bug: resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // first constraint must be protocol
    if (!lib.ResolveAsProtocol(constraints.items[0].get(), &out_params->protocol_decl))
      return lib.Fail(ErrMustBeAProtocol, constraints.items[0]->span, layout);
    out_params->protocol_decl_raw = constraints.items[0].get();

    // second constraint must be optional
    if (!lib.ResolveAsOptional(constraints.items[1].get()))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[1]->span, layout);
    out_params->nullability = types::Nullability::kNullable;
    applied_nullability_span = constraints.items[1]->span;
  } else if (num_constraints > 2) {
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(2), num_constraints);
  }

  if (protocol_decl && out_params->protocol_decl)
    return lib.Fail(ErrCannotConstrainTwice, constraints.items[0]->span, layout);
  if (!protocol_decl && !out_params->protocol_decl)
    return lib.Fail(ErrProtocolConstraintRequired, constraints.span, layout);
  const Decl* merged_protocol = protocol_decl;
  if (out_params->protocol_decl)
    merged_protocol = out_params->protocol_decl;

  bool has_nullability = nullability == types::Nullability::kNullable;
  if (has_nullability && out_params->nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, applied_nullability_span, layout);
  auto merged_nullability =
      has_nullability || out_params->nullability == types::Nullability::kNullable
          ? types::Nullability::kNullable
          : types::Nullability::kNonnullable;

  *out_type = std::make_unique<TransportSideType>(name, merged_protocol, merged_nullability, end);
  return true;
}

bool TransportSideType::ApplySomeLayoutParametersAndConstraints(
    const LibraryMediator& lib, const CreateInvocation& create_invocation,
    const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
    LayoutInvocation* out_params) const {
  assert(false && "Compiler bug: this type should only be used in the new syntax");
  return false;
}

class TypeDeclTypeTemplate final : public TypeTemplate {
 public:
  TypeDeclTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, Library* library,
                       TypeDecl* type_decl)
      : TypeTemplate(std::move(name), typespace, reporter),
        library_(library),
        type_decl_(type_decl) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    if (!type_decl_->compiled && type_decl_->kind != Decl::Kind::kProtocol) {
      if (type_decl_->compiling) {
        type_decl_->recursive = true;
      } else {
        if (!library_->CompileDecl(type_decl_)) {
          return false;
        }
      }
    }

    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 0) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);
    }

    IdentifierType type(name_, type_decl_);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    assert(!args->handle_subtype);

    if (!type_decl_->compiled && type_decl_->kind != Decl::Kind::kProtocol) {
      if (type_decl_->compiling) {
        type_decl_->recursive = true;
      } else {
        if (!library_->CompileDecl(type_decl_)) {
          return false;
        }
      }
    }

    if (args->arg_type != nullptr)
      return Fail(ErrCannotBeParameterized, args->name.span());

    IdentifierType type(name_, type_decl_);
    return type.ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type, out_params);
  }

 private:
  Library* library_;
  TypeDecl* type_decl_;
};

bool IdentifierType::ApplyConstraints(const flat::LibraryMediator& lib,
                                      const TypeConstraints& constraints,
                                      const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                                      LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  switch (type_decl->kind) {
    // These types have no allowed constraints
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kTable:
      // assume that a lone constraint was an attempt at specifying `optional` and provide a more
      // specific error
      // TOOD(fxbug.dev/75112): actually try to compile the optional constraint
      if (num_constraints == 1)
        return lib.Fail(ErrCannotBeNullable, constraints.items[0]->span, layout);
      if (num_constraints > 1) {
        return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(0),
                        num_constraints);
      }
      break;

    // These types have one allowed constraint (`optional`). For type aliases,
    // we need to allow the possibility that the concrete type does allow `optional`,
    // if it doesn't the Type itself will catch the error.
    case Decl::Kind::kTypeAlias:
    case Decl::Kind::kStruct:
    case Decl::Kind::kUnion:
      if (num_constraints > 1) {
        return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(1),
                        num_constraints);
      }
      break;

    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
      // Cannot have const: entries for constants do not exist in the typespace, so
      // they're caught earlier.
      // Cannot have resource: resource types should have resolved to the HandleTypeTemplate
      assert(false && "Compiler bug: unexpected identifier type decl kind");
      break;

    // TODO(fxbug.dev/75837):
    // These can't be used as types. This will be caught later, in VerifyTypeCategory.
    case Decl::Kind::kService:
    case Decl::Kind::kProtocol:
      break;
  }

  types::Nullability applied_nullability = types::Nullability::kNonnullable;
  if (num_constraints == 1) {
    // must be optional
    if (!lib.ResolveAsOptional(constraints.items[0].get()))
      return lib.Fail(ErrUnexpectedConstraint, constraints.items[0]->span, layout);
    applied_nullability = types::Nullability::kNullable;
  }

  if (nullability == types::Nullability::kNullable &&
      applied_nullability == types::Nullability::kNullable)
    return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
  auto merged_nullability = nullability;
  if (applied_nullability == types::Nullability::kNullable)
    merged_nullability = applied_nullability;

  out_params->nullability = applied_nullability;
  *out_type = std::make_unique<IdentifierType>(name, type_decl, merged_nullability);
  return true;
}

bool IdentifierType::ApplySomeLayoutParametersAndConstraints(
    const LibraryMediator& lib, const CreateInvocation& create_invocation,
    const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
    LayoutInvocation* out_params) const {
  switch (type_decl->kind) {
    // These types can't be nullable
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kTable:
      if (create_invocation.nullability == types::Nullability::kNullable)
        return lib.Fail(ErrCannotBeNullable, create_invocation.name.span(), layout);
      break;

    // These types have one allowed constraint (`optional`). For type aliases,
    // we need to allow the possibility that the concrete type does allow `optional`,
    // if it doesn't the Type itself will catch the error.
    case Decl::Kind::kProtocol:
    case Decl::Kind::kTypeAlias:
    case Decl::Kind::kStruct:
    case Decl::Kind::kUnion:
      if (nullability == types::Nullability::kNullable &&
          create_invocation.nullability == types::Nullability::kNullable)
        return lib.Fail(ErrCannotIndicateNullabilityTwice, std::nullopt, layout);
      break;

    // These should never be encountered
    case Decl::Kind::kConst:
    case Decl::Kind::kResource: {
      // Cannot have const: entries for constants do not exist in the typespace
      // Cannot have resource: resource types should have resolved to the HandleTypeTemplate
      assert(false);
      break;
    }

    // TODO(fxbug.dev/75837):
    // Services are not allowed to be used as types. This is caught later, during
    // VerifyTypeCategory.
    case Decl::Kind::kService:
      break;
  }

  auto merged_nullability = nullability;
  if (create_invocation.nullability == types::Nullability::kNullable)
    merged_nullability = create_invocation.nullability;

  *out_type = std::make_unique<IdentifierType>(name, type_decl, merged_nullability);
  return true;
}

class TypeAliasTypeTemplate final : public TypeTemplate {
 public:
  TypeAliasTypeTemplate(Name name, Typespace* typespace, Reporter* reporter, TypeAlias* decl)
      : TypeTemplate(std::move(name), typespace, reporter), decl_(decl) {}

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    if (!decl_->compiled) {
      if (decl_->compiling) {
        return Fail(ErrIncludeCycle);
      }

      if (!lib.CompileDecl(decl_)) {
        return false;
      }
    }

    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 0)
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(0),
                  num_params);

    // Compilation failed while trying to resolve something farther up the chain;
    // exit early
    if (!GetType(decl_->partial_type_ctor))
      return false;
    const auto& aliased_type = GetType(decl_->partial_type_ctor);
    out_params->from_type_alias = decl_;
    return aliased_type->ApplyConstraints(lib, *unresolved_args.constraints, this, out_type,
                                          out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    std::unique_ptr<CreateInvocation> args;
    if (!ResolveOldSyntaxArgs(lib, unresolved_args, &args, out_params))
      return false;

    // Note that because fidlc only populates these handle fields if it sees
    // "handle" in the type constructor, aliases of handles will never correctly
    // capture any handle constraints. It is not a TODO to fix this since this
    // issue does not exist in the new syntax.
    assert(!args->handle_subtype);
    assert(!args->handle_rights);

    if (!decl_->compiled) {
      if (decl_->compiling) {
        return Fail(ErrIncludeCycle);
      }

      if (!lib.CompileDecl(decl_)) {
        return false;
      }
    }

    if (unresolved_args.maybe_arg_type_ctor != nullptr)
      return Fail(ErrCannotParameterizeAlias, args->name.span());

    // Compilation failed while trying to resolve something farther up the chain;
    // exit early
    if (!GetType(decl_->partial_type_ctor))
      return false;
    const auto& aliased_type = GetType(decl_->partial_type_ctor);
    out_params->from_type_alias = decl_;
    return aliased_type->ApplySomeLayoutParametersAndConstraints(lib, *args, this, out_type,
                                                                 out_params);
  }

 private:
  TypeAlias* decl_;
};

class BoxTypeTemplate final : public TypeTemplate {
 public:
  BoxTypeTemplate(Typespace* typespace, Reporter* reporter)
      : TypeTemplate(Name::CreateIntrinsic("box"), typespace, reporter) {}

  static bool IsStruct(const Type* boxed_type) {
    if (!boxed_type || boxed_type->kind != Type::Kind::kIdentifier)
      return false;

    return static_cast<const IdentifierType*>(boxed_type)->type_decl->kind == Decl::Kind::kStruct;
  }

  bool Create(const LibraryMediator& lib, const NewSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    size_t num_params = unresolved_args.parameters->items.size();
    if (num_params != 1) {
      return Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.parameters->span, size_t(1),
                  num_params);
    }

    const Type* boxed_type = nullptr;
    if (!lib.ResolveParamAsType(this, unresolved_args.parameters->items[0], &boxed_type))
      return false;
    if (!IsStruct(boxed_type))
      return Fail(ErrCannotBeBoxed, boxed_type->name);
    const auto* inner = static_cast<const IdentifierType*>(boxed_type);
    if (inner->nullability == types::Nullability::kNullable) {
      reporter_->Report(ErrBoxedTypeCannotBeNullable, unresolved_args.parameters->items[0]->span);
      return false;
    }
    // We disallow specifying the boxed type as nullable in FIDL source but
    // then mark the boxed type is nullable, so that internally it shares the
    // same code path as its old syntax equivalent (a nullable struct). This
    // allows us to call `f(type)` in the old code and `f(type->boxed_type)`
    // in the new code.
    // As a temporary workaround for piping unconst-ness everywhere or having
    // box types own their own boxed types, we cast away the const to be able
    // to change the boxed type to be mutable.
    auto* mutable_inner = const_cast<IdentifierType*>(inner);
    mutable_inner->nullability = types::Nullability::kNullable;

    out_params->boxed_type_resolved = boxed_type;
    out_params->boxed_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

    BoxType type(name_, boxed_type);
    return type.ApplyConstraints(lib, *unresolved_args.constraints, this, out_type, out_params);
  }

  bool Create(const LibraryMediator& lib, const OldSyntaxParamsAndConstraints& unresolved_args,
              std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const override {
    assert(false && "Compiler bug: this type template should only be used in the new syntax");
    return false;
  }
};

bool BoxType::ApplyConstraints(const flat::LibraryMediator& lib, const TypeConstraints& constraints,
                               const TypeTemplate* layout, std::unique_ptr<Type>* out_type,
                               LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TOOD(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1)
    return lib.Fail(ErrBoxCannotBeNullable, constraints.items[0]->span);
  if (num_constraints > 1)
    return lib.Fail(ErrTooManyConstraints, constraints.span, layout, size_t(0), num_constraints);
  *out_type = std::make_unique<BoxType>(name, boxed_type);
  return true;
}

bool BoxType::ApplySomeLayoutParametersAndConstraints(const LibraryMediator& lib,
                                                      const CreateInvocation& create_invocation,
                                                      const TypeTemplate* layout,
                                                      std::unique_ptr<Type>* out_type,
                                                      LayoutInvocation* out_params) const {
  assert(false && "Compiler bug: this type should only be used in the new syntax");
  return false;
}

Typespace Typespace::RootTypes(Reporter* reporter) {
  Typespace root_typespace(reporter);

  auto add_template_old = [&](std::unique_ptr<TypeTemplate> type_template) {
    const Name& name = type_template->name();
    root_typespace.old_syntax_templates_.emplace(name, std::move(type_template));
  };

  auto add_template_new = [&](std::unique_ptr<TypeTemplate> type_template) {
    const Name& name = type_template->name();
    root_typespace.new_syntax_templates_.emplace(name, std::move(type_template));
  };

  auto add_primitive = [&](const std::string& name, types::PrimitiveSubtype subtype) {
    add_template_old(
        std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, name, subtype));
    add_template_new(
        std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, name, subtype));
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

  // TODO(fxbug.dev/7807): Remove when there is generalized support.
  const static auto kByteName = Name::CreateIntrinsic("byte");
  const static auto kBytesName = Name::CreateIntrinsic("bytes");
  root_typespace.old_syntax_templates_.emplace(
      kByteName, std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, "uint8",
                                                         types::PrimitiveSubtype::kUint8));
  root_typespace.new_syntax_templates_.emplace(
      kByteName, std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, "uint8",
                                                         types::PrimitiveSubtype::kUint8));
  root_typespace.old_syntax_templates_.emplace(
      kBytesName, std::make_unique<BytesTypeTemplate>(&root_typespace, reporter));
  root_typespace.new_syntax_templates_.emplace(
      kBytesName, std::make_unique<BytesTypeTemplate>(&root_typespace, reporter));

  add_template_old(std::make_unique<ArrayTypeTemplate>(&root_typespace, reporter));
  add_template_new(std::make_unique<ArrayTypeTemplate>(&root_typespace, reporter));
  add_template_old(std::make_unique<VectorTypeTemplate>(&root_typespace, reporter));
  add_template_new(std::make_unique<VectorTypeTemplate>(&root_typespace, reporter));
  add_template_old(std::make_unique<StringTypeTemplate>(&root_typespace, reporter));
  add_template_new(std::make_unique<StringTypeTemplate>(&root_typespace, reporter));
  add_template_old(std::make_unique<HandleTypeTemplate>(&root_typespace, reporter));
  add_template_new(std::make_unique<HandleTypeTemplate>(&root_typespace, reporter));

  // add syntax specific typespace entries
  // TODO(fxbug.dev/70247): consolidate maps
  auto request = std::make_unique<RequestTypeTemplate>(&root_typespace, reporter);
  root_typespace.old_syntax_templates_.emplace(request->name(), std::move(request));
  auto server_end = std::make_unique<TransportSideTypeTemplate>(&root_typespace, reporter,
                                                                TransportSide::kServer);
  root_typespace.new_syntax_templates_.emplace(server_end->name(), std::move(server_end));

  auto client_end = std::make_unique<TransportSideTypeTemplate>(&root_typespace, reporter,
                                                                TransportSide::kClient);
  root_typespace.new_syntax_templates_.emplace(client_end->name(), std::move(client_end));

  auto box = std::make_unique<BoxTypeTemplate>(&root_typespace, reporter);
  root_typespace.new_syntax_templates_.emplace(box->name(), std::move(box));
  return root_typespace;
}

void AttributeArgSchema::ValidateValue(Reporter* reporter, const MaybeAttributeArg maybe_arg,
                                       const std::unique_ptr<Attribute>& attribute) const {
  // This argument was not specified - is that allowed?
  if (!maybe_arg.has_value()) {
    if (!IsOptional()) {
      reporter->Report(ErrMissingRequiredAttributeArg, attribute->span(), attribute.get(), name_);
    }
  }
}

AttributeSchema::AttributeSchema(const std::set<AttributePlacement>& allowed_placements,
                                 const std::map<std::string, AttributeArgSchema>& arg_schemas,
                                 Constraint constraint)
    : allowed_placements_(allowed_placements),
      arg_schemas_(arg_schemas),
      constraint_(std::move(constraint)) {}

AttributeSchema AttributeSchema::Deprecated() {
  return AttributeSchema({AttributePlacement::kDeprecated});
}

bool AttributeSchema::ValidatePlacement(Reporter* reporter,
                                        const std::unique_ptr<Attribute>& attribute,
                                        const Attributable* attributable) const {
  if (allowed_placements_.empty()) {
    return true;
  }

  if (IsDeprecated()) {
    reporter->Report(ErrDeprecatedAttribute, attribute->span(), attribute.get());
    return false;
  }

  if (allowed_placements_.size() == 1 &&
      *allowed_placements_.cbegin() == AttributePlacement::kAnonymousLayout) {
    switch (attributable->placement) {
      case AttributePlacement::kBitsDecl:
      case AttributePlacement::kEnumDecl:
      case AttributePlacement::kStructDecl:
      case AttributePlacement::kTableDecl:
      case AttributePlacement::kUnionDecl: {
        const auto* decl = static_cast<const Decl*>(attributable);
        if (decl->name.as_anonymous() == nullptr) {
          reporter->Report(ErrInvalidAttributePlacement, attribute->span(), attribute.get());
          return false;
        }
        return true;
      }
      default:
        reporter->Report(ErrInvalidAttributePlacement, attribute->span(), attribute.get());
        return false;
    }
  }

  auto iter = allowed_placements_.find(attributable->placement);
  if (iter != allowed_placements_.end())
    return true;
  reporter->Report(ErrInvalidAttributePlacement, attribute->span(), attribute.get());
  return false;
}

bool AttributeSchema::ValidateArgs(Reporter* reporter,
                                   const std::unique_ptr<Attribute>& attribute) const {
  // An attribute that has already been resolved (for example, on a composed method that is
  // referenced via pointer by its compositor) is assumed to be valid, since that prior resolution
  // would have needed to have successfully called ValidateArgs already.
  if (attribute->resolved) {
    return true;
  }

  bool ok = true;
  // If this attribute is deprecated, this fact would have already been caught and reported when
  // its placement was validated, so we can just return silently.
  if (IsDeprecated()) {
    return true;
  }

  // There are two distinct cases to handle here: a single, unnamed argument (`@foo("abc")`), and
  // zero or more named arguments (`@foo`, `@foo(bar="abc")` or `@foo(bar="abc",baz="def")`).
  MaybeAttributeArg anon_arg = attribute->GetStandaloneAnonymousArg();
  if (anon_arg.has_value()) {
    // Error if the user supplied an anonymous argument, like `@foo("abc")` for an attribute whose
    // schema specifies multiple arguments (and therefore requires that they always be named).
    if (arg_schemas_.size() == 0) {
      reporter->Report(ErrAttributeDisallowsArgs, attribute->span(), attribute.get());
      ok = false;
    } else if (arg_schemas_.size() > 1) {
      reporter->Report(ErrAttributeArgNotNamed, attribute->span(), &anon_arg.value().get());
      ok = false;
    }

    // We've verified that we are expecting a single argument, and that we have a single anonymous
    // argument that we can validate as an instance of it.
    for (const auto& arg_schema : arg_schemas_) {
      const auto& schema = arg_schema.second;
      schema.ValidateValue(reporter, anon_arg, attribute);
    }
  } else {
    // If we have a single-arg official attribute its argument must always be anonymous, like
    // `@transport("foo")`. Check if the user wrote this as a named argument, and error if they did.
    if (arg_schemas_.size() == 1 && attribute->args.size() == 1) {
      reporter->Report(ErrAttributeArgMustNotBeNamed, attribute->span());
      ok = false;
    }

    // All of the arguments should be named - compare each argument schema against its (possible)
    // value.
    for (const auto& arg_schema : arg_schemas_) {
      const auto& name = arg_schema.first;
      const auto& schema = arg_schema.second;
      MaybeAttributeArg arg = attribute->GetArg(name);
      schema.ValidateValue(reporter, arg, attribute);
    }

    // Make sure that no arguments not specified by the schema sneak through.
    for (const auto& arg : attribute->args) {
      assert(arg->name.has_value() && "anonymous arguments should not be seen here");
      auto schema = arg_schemas_.find(arg->name.value());
      if (schema == arg_schemas_.end()) {
        reporter->Report(ErrUnknownAttributeArg, attribute->span(), attribute.get(),
                         arg->name.value());
        ok = false;
      }
    }
  }
  return ok;
}

bool AttributeSchema::ValidateConstraint(Reporter* reporter,
                                         const std::unique_ptr<Attribute>& attribute,
                                         const Attributable* attributable) const {
  assert(attributable);
  auto check = reporter->Checkpoint();
  auto passed = constraint_(reporter, attribute, attributable);
  if (passed) {
    assert(check.NoNewErrors() && "cannot add errors and pass");
    return true;
  }
  if (check.NoNewErrors()) {
    reporter->Report(ErrAttributeConstraintNotSatisfied, attribute->span(), attribute.get());
  }
  return false;
}

bool AttributeSchema::ResolveArgs(Library* library, std::unique_ptr<Attribute>& attribute) const {
  if (attribute->resolved) {
    return true;
  }

  // For attributes with a single, anonymous argument like `@foo("bar")`, use the schema to assign
  // that argument a name.
  if (attribute->HasStandaloneAnonymousArg()) {
    assert(arg_schemas_.size() == 1 && "expected a schema with only one value");
    for (const auto& arg_schema : arg_schemas_) {
      attribute->args[0]->name = arg_schema.first;
    }
  }

  // Resolve each constant as its schema-specified type.
  bool ok = true;
  for (auto& arg : attribute->args) {
    auto found = arg_schemas_.find(arg->name.value());
    assert(found != arg_schemas_.end() && "did we call ValidateArgs before ResolveArgs?");

    const auto arg_schema = found->second;
    const auto want_type = arg_schema.Type();
    switch (want_type) {
      case ConstantValue::Kind::kDocComment:
      case ConstantValue::Kind::kString: {
        static const auto max_size = Size::Max();
        static const StringType kUnboundedStringType = StringType(
            Name::CreateIntrinsic("string"), &max_size, types::Nullability::kNonnullable);
        if (!library->ResolveConstant(arg->value.get(), &kUnboundedStringType)) {
          ok = false;
        }
        break;
      }
      case ConstantValue::Kind::kBool:
      case ConstantValue::Kind::kInt8:
      case ConstantValue::Kind::kInt16:
      case ConstantValue::Kind::kInt32:
      case ConstantValue::Kind::kInt64:
      case ConstantValue::Kind::kUint8:
      case ConstantValue::Kind::kUint16:
      case ConstantValue::Kind::kUint32:
      case ConstantValue::Kind::kUint64:
      case ConstantValue::Kind::kFloat32:
      case ConstantValue::Kind::kFloat64: {
        const std::string primitive_name = ConstantValue::KindToIntrinsicName(want_type);
        const std::optional<types::PrimitiveSubtype> primitive_subtype =
            ConstantValue::KindToPrimitiveSubtype(want_type);
        assert(primitive_subtype.has_value());

        const auto primitive_type =
            PrimitiveType(Name::CreateIntrinsic(primitive_name), primitive_subtype.value());
        if (!library->ResolveConstant(arg->value.get(), &primitive_type)) {
          ok = false;
        }
        break;
      }
    }
  }

  attribute->resolved = ok;
  return ok;
}

bool SimpleLayoutConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attr,
                            const Attributable* attributable) {
  assert(attributable);
  switch (attributable->placement) {
    case AttributePlacement::kStructDecl: {
      auto struct_decl = static_cast<const Struct*>(attributable);
      bool ok = true;
      for (const auto& member : struct_decl->members) {
        if (!IsSimple(GetType(member.type_ctor), reporter)) {
          reporter->Report(ErrMemberMustBeSimple, member.name, member.name.data());
          ok = false;
        }
      }
      return ok;
    }
    case AttributePlacement::kMethod: {
      auto method = static_cast<const Protocol::Method*>(attributable);
      if (method->maybe_request_payload &&
          !SimpleLayoutConstraint(reporter, attr, method->maybe_request_payload)) {
        return false;
      }
      if (method->maybe_response_payload &&
          !SimpleLayoutConstraint(reporter, attr, method->maybe_response_payload)) {
        return false;
      }
      return true;
    }
    default:
      assert(false && "unexpected kind");
  }

  __builtin_unreachable();
}

bool ParseBound(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                const std::string& input, uint32_t* out_value) {
  auto result = utils::ParseNumeric(input, out_value, 10);
  switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
      reporter->Report(ErrBoundIsTooBig, attribute->span(), attribute.get(), input);
      return false;
    case utils::ParseNumericResult::kMalformed: {
      reporter->Report(ErrUnableToParseBound, attribute->span(), attribute.get(), input);
      return false;
    }
    case utils::ParseNumericResult::kSuccess:
      return true;
  }
}

bool Library::VerifyInlineSize(const Struct* struct_decl) {
  if (struct_decl->typeshape(WireFormat::kV1NoEe).InlineSize() >= 65536) {
    return Library::Fail(ErrInlineSizeExceeds64k, *struct_decl);
  }
  return true;
}

bool OverrideNameConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                            const Attributable* attributable) {
  auto arg = attribute->GetArg("value");
  if (!arg.has_value()) {
    reporter->Report(ErrMissingRequiredAnonymousAttributeArg, attribute->span(), attribute.get());
    return false;
  }
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg.value().get().value->Value());

  if (!utils::IsValidIdentifierComponent(arg_value.MakeContents())) {
    reporter->Report(ErrInvalidNameOverride, attribute->span());
    return false;
  }
  return true;
}

bool MaxBytesConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                        const Attributable* attributable) {
  assert(attributable);
  auto arg = attribute->GetArg("value");
  if (!arg.has_value() ||
      arg.value().get().value->Value().kind != flat::ConstantValue::Kind::kString) {
    assert(false && "non-string attribute arguments not yet supported");
  }
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg.value().get().value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
  switch (attributable->placement) {
    case AttributePlacement::kStructDecl: {
      auto struct_decl = static_cast<const Struct*>(attributable);
      max_bytes = struct_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  struct_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case AttributePlacement::kTableDecl: {
      auto table_decl = static_cast<const Table*>(attributable);
      max_bytes = table_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  table_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    case AttributePlacement::kUnionDecl: {
      auto union_decl = static_cast<const Union*>(attributable);
      max_bytes = union_decl->typeshape(WireFormat::kV1NoEe).InlineSize() +
                  union_decl->typeshape(WireFormat::kV1NoEe).MaxOutOfLine();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_bytes > bound) {
    reporter->Report(ErrTooManyBytes, attribute->span(), bound, max_bytes);
    return false;
  }
  return true;
}

bool MaxHandlesConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                          const Attributable* attributable) {
  assert(attributable);
  auto arg = attribute->GetArg("value");
  if (!arg.has_value() ||
      arg.value().get().value->Value().kind != flat::ConstantValue::Kind::kString) {
    reporter->Report(ErrInvalidAttributeType, attribute->span(), attribute.get());
    assert(false && "non-string attribute arguments not yet supported");
  }
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg.value().get().value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_handles = std::numeric_limits<uint32_t>::max();
  switch (attributable->placement) {
    case AttributePlacement::kStructDecl: {
      auto struct_decl = static_cast<const Struct*>(attributable);
      max_handles = struct_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case AttributePlacement::kTableDecl: {
      auto table_decl = static_cast<const Table*>(attributable);
      max_handles = table_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    case AttributePlacement::kUnionDecl: {
      auto union_decl = static_cast<const Union*>(attributable);
      max_handles = union_decl->typeshape(WireFormat::kV1NoEe).MaxHandles();
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_handles > bound) {
    reporter->Report(ErrTooManyHandles, attribute->span(), bound, max_handles);
    return false;
  }
  return true;
}

bool ResultShapeConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                           const Attributable* attributable) {
  assert(attributable);
  assert(attributable->placement == AttributePlacement::kUnionDecl);
  auto union_decl = static_cast<const Union*>(attributable);
  assert(union_decl->members.size() == 2);
  auto& error_member = union_decl->members.at(1);
  assert(error_member.maybe_used && "must have an error member");
  auto error_type = GetType(error_member.maybe_used->type_ctor);

  const PrimitiveType* error_primitive = nullptr;
  if (error_type->kind == Type::Kind::kPrimitive) {
    error_primitive = static_cast<const PrimitiveType*>(error_type);
  } else if (error_type->kind == Type::Kind::kIdentifier) {
    auto identifier_type = static_cast<const IdentifierType*>(error_type);
    if (identifier_type->type_decl->kind == Decl::Kind::kEnum) {
      auto error_enum = static_cast<const Enum*>(identifier_type->type_decl);
      assert(GetType(error_enum->subtype_ctor)->kind == Type::Kind::kPrimitive);
      error_primitive = static_cast<const PrimitiveType*>(GetType(error_enum->subtype_ctor));
    }
  }

  if (!error_primitive || (error_primitive->subtype != types::PrimitiveSubtype::kInt32 &&
                           error_primitive->subtype != types::PrimitiveSubtype::kUint32)) {
    reporter->Report(ErrInvalidErrorType, union_decl->name.span());
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

bool TransportConstraint(Reporter* reporter, const std::unique_ptr<Attribute>& attribute,
                         const Attributable* attributable) {
  assert(attributable);
  assert(attributable->placement == AttributePlacement::kMethod);
  auto method = static_cast<const Protocol::Method*>(attributable);

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kValidTransports = new std::set<std::string>{
      "Banjo",
      "Channel",
      "Syscall",
  };

  auto arg = attribute->GetArg("value");
  if (!arg.has_value()) {
    reporter->Report(ErrInvalidTransportType, method->name, std::string("''"), *kValidTransports);
    return false;
  }
  if (arg.value().get().value->Value().kind != flat::ConstantValue::Kind::kString)
    assert(false && "non-string attribute arguments not yet supported");
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg.value().get().value->Value());

  // Parse comma separated transports
  const std::string& value = arg_value.MakeContents();
  std::string::size_type prev_pos = 0;
  std::string::size_type pos;
  std::vector<std::string> transports;
  while ((pos = value.find(',', prev_pos)) != std::string::npos) {
    transports.emplace_back(Trim(value.substr(prev_pos, pos - prev_pos)));
    prev_pos = pos + 1;
  }
  transports.emplace_back(Trim(value.substr(prev_pos)));

  // Validate that they're ok
  for (auto transport : transports) {
    if (kValidTransports->count(transport) == 0) {
      reporter->Report(ErrInvalidTransportType, method->name, transport, *kValidTransports);
      return false;
    }
  }
  return true;
}

Resource::Property* Resource::LookupProperty(std::string_view name) {
  for (Property& property : properties) {
    if (property.name.data() == name.data()) {
      return &property;
    }
  }
  return nullptr;
}

Libraries::Libraries() {
  // clang-format off
  AddAttributeSchema("discoverable", AttributeSchema({
    AttributePlacement::kProtocolDecl,
  }));
  AddAttributeSchema("doc", AttributeSchema({
    /* any placement */
  }, AttributeArgSchema("text", ConstantValue::Kind::kString)));
  AddAttributeSchema("layout", AttributeSchema::Deprecated()),
  AddAttributeSchema("for_deprecated_c_bindings", AttributeSchema({
    AttributePlacement::kProtocolDecl,
    AttributePlacement::kStructDecl,
  }, SimpleLayoutConstraint));
  AddAttributeSchema("generated_name", AttributeSchema({
    AttributePlacement::kAnonymousLayout,
  }, AttributeArgSchema(ConstantValue::Kind::kString),
  OverrideNameConstraint)),
  AddAttributeSchema("max_bytes", AttributeSchema({
    AttributePlacement::kProtocolDecl,
    AttributePlacement::kMethod,
    AttributePlacement::kStructDecl,
    AttributePlacement::kTableDecl,
    AttributePlacement::kUnionDecl,
  }, AttributeArgSchema(ConstantValue::Kind::kString),
  MaxBytesConstraint));
  AddAttributeSchema("max_handles", AttributeSchema({
    AttributePlacement::kProtocolDecl,
    AttributePlacement::kMethod,
    AttributePlacement::kStructDecl,
    AttributePlacement::kTableDecl,
    AttributePlacement::kUnionDecl,
  }, AttributeArgSchema(ConstantValue::Kind::kString),
  MaxHandlesConstraint));
  AddAttributeSchema("result", AttributeSchema({
    AttributePlacement::kUnionDecl,
  }, ResultShapeConstraint));
  AddAttributeSchema("selector", AttributeSchema({
    AttributePlacement::kMethod,
  }, AttributeArgSchema(ConstantValue::Kind::kString)));
  AddAttributeSchema("transitional", AttributeSchema({
    AttributePlacement::kMethod,
    AttributePlacement::kBitsDecl,
    AttributePlacement::kEnumDecl,
    AttributePlacement::kUnionDecl,
  }, AttributeArgSchema("reason", ConstantValue::Kind::kString, AttributeArgSchema::Optionality::kOptional)));
  AddAttributeSchema("transport", AttributeSchema({
    AttributePlacement::kProtocolDecl,
  }, AttributeArgSchema("types", ConstantValue::Kind::kString), TransportConstraint));
  AddAttributeSchema("unknown", AttributeSchema({
    AttributePlacement::kEnumMember,
    AttributePlacement::kUnionMember,
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

const AttributeSchema* Libraries::RetrieveAttributeSchema(
    Reporter* reporter, const std::unique_ptr<Attribute>& attribute, fidl::utils::Syntax syntax,
    bool warn_on_typo) const {
  auto attribute_name = attribute->name;

  // TODO(fxbug.dev/70247): once the migration is complete, we no longer need to
  //  do the the casting to lower_snake_case, so this check should be removed.
  if (syntax == fidl::utils::Syntax::kOld) {
    attribute_name = fidl::utils::to_lower_snake_case(attribute->name);
  }

  auto iter = attribute_schemas_.find(attribute_name);
  if (iter != attribute_schemas_.end()) {
    const auto& schema = iter->second;
    return &schema;
  }

  // Skip typo check?
  if (reporter == nullptr || !warn_on_typo) {
    return nullptr;
  }

  // Match against all known attributes.
  for (const auto& name_and_schema : attribute_schemas_) {
    std::string supplied_name = attribute_name;
    std::string suspected_name = name_and_schema.first;

    // TODO(fxbug.dev/70247): once the migration is complete, we no longer need
    //  to do the the casting to lower_snake_case, so this check should be
    //  removed.
    if (syntax == fidl::utils::Syntax::kOld) {
      supplied_name = attribute->name;
      suspected_name = fidl::utils::to_upper_camel_case(name_and_schema.first);
    }

    auto edit_distance = EditDistance(supplied_name, suspected_name);
    if (0 < edit_distance && edit_distance < 2) {
      reporter->Report(WarnAttributeTypo, attribute->span(), supplied_name, suspected_name);
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

bool Dependencies::Lookup(std::string_view filename, const std::vector<std::string_view>& name,
                          Dependencies::LookupMode mode, Library** out_library) const {
  auto iter1 = dependencies_.find(std::string(filename));
  if (iter1 == dependencies_.end()) {
    return false;
  }

  auto iter2 = iter1->second->find(name);
  if (iter2 == iter1->second->end()) {
    return false;
  }

  auto ref = iter2->second;
  if (mode == Dependencies::LookupMode::kUse) {
    ref->used_ = true;
  }
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
      reporter->Report(ErrUnusedImport, ref->span_, for_library.name(), ref->library_->name(),
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
    return utils::StringJoin(library->name(), separator);
  }
  return std::string();
}

bool Library::Fail(std::unique_ptr<Diagnostic> err) {
  assert(err && "should not report nullptr error");
  reporter_->Report(std::move(err));
  return false;
}

template <typename... Args>
bool Library::Fail(const ErrorDef<Args...>& err, const Args&... args) {
  reporter_->Report(err, args...);
  return false;
}

template <typename... Args>
bool Library::Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                   const Args&... args) {
  reporter_->Report(err, span, args...);
  return false;
}

bool Library::ValidateAttributesPlacement(const Attributable* attributable) {
  bool ok = true;
  if (attributable == nullptr || attributable->attributes == nullptr)
    return ok;
  for (const auto& attribute : attributable->attributes->attributes) {
    auto schema = all_libraries_->RetrieveAttributeSchema(reporter_, attribute, attribute->syntax);
    if (schema != nullptr && !schema->ValidatePlacement(reporter_, attribute, attributable)) {
      ok = false;
    }
  }
  return ok;
}

bool Library::ValidateAttributesConstraints(const Attributable* attributable) {
  if (attributable == nullptr || attributable->attributes == nullptr)
    return true;
  return ValidateAttributesConstraints(attributable, attributable->attributes.get());
}

bool Library::ValidateAttributesConstraints(const Attributable* attributable,
                                            const AttributeList* attributes) {
  bool ok = true;
  if (attributable == nullptr || attributes == nullptr)
    return ok;
  for (const auto& attribute : attributes->attributes) {
    auto schema = all_libraries_->RetrieveAttributeSchema(nullptr, attribute, attribute->syntax);
    if (schema != nullptr && !schema->ValidateConstraint(reporter_, attribute, attributable)) {
      ok = false;
    }
  }
  return ok;
}

bool Library::LookupDependency(std::string_view filename, const std::vector<std::string_view>& name,
                               Library** out_library) const {
  return dependencies_.Lookup(filename, name, Dependencies::LookupMode::kSilent, out_library);
}

SourceSpan Library::GeneratedSimpleName(const std::string& name) {
  return generated_source_file_.AddLine(name);
}

std::string Library::NextAnonymousName() {
  // TODO(fxbug.dev/7920): Improve anonymous name generation. We want to be
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
  if (dependencies_.Lookup(filename, library_name, Dependencies::LookupMode::kUse, &dep_library)) {
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
  if (dependencies_.Lookup(filename, member_library_name, Dependencies::LookupMode::kUse,
                           &member_dep_library)) {
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
      auto type_alias_template =
          std::make_unique<TypeAliasTypeTemplate>(name, typespace_, reporter_, type_alias_decl);
      typespace_->AddTemplate(std::move(type_alias_template));
      break;
    }
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
      break;
  }  // switch
  return true;
}

ConsumeStep Library::StartConsumeStep(fidl::utils::Syntax syntax) {
  return ConsumeStep(this, syntax);
}
CompileStep Library::StartCompileStep() { return CompileStep(this); }
VerifyResourcenessStep Library::StartVerifyResourcenessStep() {
  return VerifyResourcenessStep(this);
}
VerifyAttributesStep Library::StartVerifyAttributesStep() { return VerifyAttributesStep(this); }

bool Library::ConsumeAttributeListOld(std::unique_ptr<raw::AttributeListOld> raw_attribute_list,
                                      std::unique_ptr<AttributeList>* out_attribute_list) {
  AttributesBuilder<Attribute> attributes_builder(reporter_);
  if (raw_attribute_list) {
    for (auto& raw_attribute : raw_attribute_list->attributes) {
      std::vector<std::unique_ptr<AttributeArg>> args;
      if (raw_attribute.value) {
        auto constant = std::make_unique<LiteralConstant>(std::move(raw_attribute.value));
        args.emplace_back(std::make_unique<AttributeArg>(std::nullopt, std::move(constant),
                                                         raw_attribute.span()));
      }
      auto attribute = std::make_unique<Attribute>(raw_attribute.name, fidl::utils::Syntax::kOld,
                                                   raw_attribute.span(), std::move(args));
      attributes_builder.Insert(std::move(attribute));
    }
  }

  auto attributes = attributes_builder.Done();
  *out_attribute_list = std::make_unique<AttributeList>(std::move(attributes));
  return true;
}

bool Library::ConsumeAttributeListNew(std::unique_ptr<raw::AttributeListNew> raw_attribute_list,
                                      std::unique_ptr<AttributeList>* out_attribute_list) {
  AttributesBuilder<Attribute> attributes_builder(reporter_);
  if (raw_attribute_list != nullptr) {
    for (auto& raw_attribute : raw_attribute_list->attributes) {
      std::vector<std::unique_ptr<AttributeArg>> args;
      for (auto& raw_arg : raw_attribute->args) {
        std::unique_ptr<Constant> constant;
        if (!ConsumeConstant(std::move(raw_arg->value), &constant)) {
          return false;
        }

        args.emplace_back(
            std::make_unique<AttributeArg>(raw_arg->name, std::move(constant), raw_arg->span()));
      }
      auto attribute = std::make_unique<Attribute>(raw_attribute->name, fidl::utils::Syntax::kNew,
                                                   raw_attribute->span(), std::move(args));
      attributes_builder.Insert(std::move(attribute));
    }
  }

  auto attributes = attributes_builder.Done();
  *out_attribute_list = std::make_unique<AttributeList>(std::move(attributes));
  return true;
}

bool Library::ConsumeAttributeList(raw::AttributeList raw_attribute_list,
                                   std::unique_ptr<AttributeList>* out_attribute_list) {
  return std::visit(fidl::utils::matchers{
                        [&, this](std::unique_ptr<raw::AttributeListOld> e) -> bool {
                          return ConsumeAttributeListOld(std::move(e), out_attribute_list);
                        },
                        [&, this](std::unique_ptr<raw::AttributeListNew> e) -> bool {
                          return ConsumeAttributeListNew(std::move(e), out_attribute_list);
                        },
                    },
                    std::move(raw_attribute_list));
}

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
      std::unique_ptr<LiteralConstant> out;
      ConsumeLiteralConstant(literal, &out);
      *out_constant = std::unique_ptr<Constant>(out.release());
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

void Library::ConsumeLiteralConstant(raw::LiteralConstant* raw_constant,
                                     std::unique_ptr<LiteralConstant>* out_constant) {
  *out_constant = std::make_unique<LiteralConstant>(std::move(raw_constant->literal));
}

bool Library::ConsumeTypeConstructorOld(std::unique_ptr<raw::TypeConstructorOld> raw_type_ctor,
                                        std::unique_ptr<TypeConstructorOld>* out_type_ctor) {
  auto name = CompileCompoundIdentifier(raw_type_ctor->identifier.get());
  if (!name)
    return false;

  std::unique_ptr<TypeConstructorOld> maybe_arg_type_ctor;
  if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
    if (!ConsumeTypeConstructorOld(std::move(raw_type_ctor->maybe_arg_type_ctor),
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

  std::optional<Name> handle_subtype_identifier;
  if (raw_type_ctor->handle_subtype_identifier) {
    handle_subtype_identifier =
        Name::CreateSourced(this, raw_type_ctor->handle_subtype_identifier->span());
  }

  *out_type_ctor = std::make_unique<TypeConstructorOld>(
      std::move(name.value()), std::move(maybe_arg_type_ctor), std::move(handle_subtype_identifier),
      std::move(handle_rights), std::move(maybe_size), raw_type_ctor->nullability);
  return true;
}

void Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
  if (raw::IsAttributeListNotEmpty(using_directive->attributes)) {
    std::visit(fidl::utils::matchers{
                   [&](const std::unique_ptr<raw::AttributeListOld>& attributes) -> void {
                     Fail(ErrAttributesOldNotAllowedOnLibraryImport, using_directive->span(),
                          attributes.get());
                   },
                   [&](const std::unique_ptr<raw::AttributeListNew>& attributes) -> void {
                     Fail(ErrAttributesNewNotAllowedOnLibraryImport, using_directive->span(),
                          attributes.get());
                   },
               },
               using_directive->attributes);
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

bool Library::ConsumeTypeAlias(std::unique_ptr<raw::AliasDeclaration> alias_declaration) {
  assert(alias_declaration->alias && IsTypeConstructorDefined(alias_declaration->type_ctor));

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(alias_declaration->attributes), &attributes)) {
    return false;
  }

  auto alias_name = Name::CreateSourced(this, alias_declaration->alias->span());
  TypeConstructor type_ctor_;

  if (!ConsumeTypeConstructor(std::move(alias_declaration->type_ctor),
                              NamingContext::Create(alias_name), &type_ctor_))
    return false;

  return RegisterDecl(std::make_unique<TypeAlias>(std::move(attributes), std::move(alias_name),
                                                  std::move(type_ctor_)));
}

void Library::ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
  std::vector<Bits::Member> members;
  for (auto& member : bits_declaration->members) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return;
    }
    auto span = member->identifier->span();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return;
    members.emplace_back(span, std::move(value), std::move(attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the bits name qualifier such as
    // <name_of_bits>.<name_of_member>.
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(bits_declaration->attributes), &attributes)) {
    return;
  }

  std::unique_ptr<TypeConstructorOld> type_ctor;
  if (bits_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructorOld(std::move(bits_declaration->maybe_type_ctor), &type_ctor))
      return;
  } else {
    type_ctor = TypeConstructorOld::CreateSizeType();
  }

  RegisterDecl(std::make_unique<Bits>(
      std::move(attributes), Name::CreateSourced(this, bits_declaration->identifier->span()),
      std::move(type_ctor), std::move(members), bits_declaration->strictness));
}

void Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
  auto span = const_declaration->identifier->span();
  auto name = Name::CreateSourced(this, span);
  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(const_declaration->attributes), &attributes)) {
    return;
  }

  TypeConstructor type_ctor;
  if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), NamingContext::Create(name),
                              &type_ctor))
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
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return;
    }

    auto span = member->identifier->span();
    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return;
    members.emplace_back(span, std::move(value), std::move(attributes));
    // TODO(pascallouis): right now, members are not registered. Look into
    // registering them, potentially under the enum name qualifier such as
    // <name_of_enum>.<name_of_member>.
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(enum_declaration->attributes), &attributes)) {
    return;
  }

  std::unique_ptr<TypeConstructorOld> type_ctor;
  if (enum_declaration->maybe_type_ctor) {
    if (!ConsumeTypeConstructorOld(std::move(enum_declaration->maybe_type_ctor), &type_ctor))
      return;
  } else {
    type_ctor = TypeConstructorOld::CreateSizeType();
  }

  RegisterDecl(std::make_unique<Enum>(
      std::move(attributes), Name::CreateSourced(this, enum_declaration->identifier->span()),
      std::move(type_ctor), std::move(members), enum_declaration->strictness));
}

namespace {

// Create a type constructor pointing to an anonymous layout.
std::unique_ptr<TypeConstructorNew> IdentifierTypeForDecl(const Decl* decl) {
  std::vector<std::unique_ptr<LayoutParameter>> no_params;
  std::vector<std::unique_ptr<Constant>> no_constraints;
  return std::make_unique<TypeConstructorNew>(
      decl->name, std::make_unique<LayoutParameterList>(std::move(no_params), std::nullopt),
      std::make_unique<TypeConstraints>(std::move(no_constraints), std::nullopt));
}

}  // namespace

bool Library::CreateMethodResult(const std::shared_ptr<NamingContext>& err_variant_context,
                                 SourceSpan response_span, raw::ProtocolMethod* method,
                                 Struct* success_variant, Struct** out_response) {
  // Compile the error type.
  flat::TypeConstructor error_type_ctor;
  if (!ConsumeTypeConstructor(std::move(method->maybe_error_ctor), err_variant_context,
                              &error_type_ctor))
    return false;

  raw::SourceElement sourceElement = raw::SourceElement(fidl::Token(), fidl::Token());
  assert(success_variant->name.as_anonymous() != nullptr);
  auto success_variant_context = success_variant->name.as_anonymous()->context;
  Union::Member success_member{
      std::make_unique<raw::Ordinal64>(sourceElement,
                                       1),  // success case explicitly has ordinal 1
      IdentifierTypeForDecl(success_variant), success_variant_context->name(), nullptr};
  Union::Member error_member{
      std::make_unique<raw::Ordinal64>(sourceElement, 2),  // error case explicitly has ordinal 2
      std::move(error_type_ctor), err_variant_context->name(), nullptr};
  std::vector<Union::Member> result_members;
  result_members.push_back(std::move(success_member));
  result_members.push_back(std::move(error_member));
  std::vector<std::unique_ptr<Attribute>> result_attributes;
  result_attributes.emplace_back(std::make_unique<Attribute>("result", fidl::utils::Syntax::kNew));

  // TODO(fxbug.dev/8027): Join spans of response and error constructor for `result_name`.
  auto result_context = err_variant_context->parent();
  auto result_name = Name::CreateAnonymous(this, response_span, result_context);
  auto union_decl = std::make_unique<Union>(
      std::make_unique<AttributeList>(std::move(result_attributes)), std::move(result_name),
      std::move(result_members), types::Strictness::kStrict, std::nullopt /* resourceness */);
  auto result_decl = union_decl.get();
  if (!RegisterDecl(std::move(union_decl)))
    return false;

  // Make a new response struct for the method containing just the
  // result union.
  std::vector<Struct::Member> response_members;
  response_members.push_back(
      Struct::Member(IdentifierTypeForDecl(result_decl), result_context->name(), nullptr, nullptr));

  const auto& response_context = result_context->parent();
  auto struct_decl = std::make_unique<Struct>(
      nullptr /* attributes */, Name::CreateAnonymous(this, response_span, response_context),
      std::move(response_members), std::nullopt /* resourceness */,
      true /* is_request_or_response */);
  auto struct_decl_ptr = struct_decl.get();
  if (!RegisterDecl(std::move(struct_decl)))
    return false;
  *out_response = struct_decl_ptr;
  return true;
}

void Library::ConsumeProtocolDeclaration(
    std::unique_ptr<raw::ProtocolDeclaration> protocol_declaration) {
  auto protocol_name = Name::CreateSourced(this, protocol_declaration->identifier->span());
  auto protocol_context = NamingContext::Create(protocol_name.span().value());

  std::vector<Protocol::ComposedProtocol> composed_protocols;
  std::set<Name> seen_composed_protocols;
  for (auto& raw_composed : protocol_declaration->composed_protocols) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(raw_composed->attributes), &attributes)) {
      return;
    }

    auto& raw_protocol_name = raw_composed->protocol_name;
    auto composed_protocol_name = CompileCompoundIdentifier(raw_protocol_name.get());
    if (!composed_protocol_name)
      return;
    if (!seen_composed_protocols.insert(composed_protocol_name.value()).second) {
      Fail(ErrProtocolComposedMultipleTimes, composed_protocol_name->span());
      return;
    }

    composed_protocols.emplace_back(std::move(attributes),
                                    std::move(composed_protocol_name.value()));
  }

  std::vector<Protocol::Method> methods;
  for (auto& method : protocol_declaration->methods) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(method->attributes), &attributes)) {
      return;
    }

    SourceSpan method_name = method->identifier->span();
    bool has_request = raw::IsParameterListDefined(method->maybe_request);
    Struct* maybe_request = nullptr;
    if (has_request) {
      bool result = std::visit(
          [this, method_name, &maybe_request, &protocol_context](auto params) -> bool {
            return ConsumeParameterList(method_name, protocol_context->EnterRequest(method_name),
                                        std::move(params), true, &maybe_request);
          },
          std::move(method->maybe_request));
      if (!result)
        return;
    }

    Struct* maybe_response = nullptr;
    bool has_response = raw::IsParameterListDefined(method->maybe_response);
    if (has_response) {
      const bool has_error = raw::IsTypeConstructorDefined(method->maybe_error_ctor);

      SourceSpan response_span = raw::GetSpan(method->maybe_response);
      auto response_context = has_request ? protocol_context->EnterResponse(method_name)
                                          : protocol_context->EnterEvent(method_name);

      std::shared_ptr<NamingContext> result_context, success_variant_context, err_variant_context;
      if (has_error) {
        // The error syntax for protocol P and method M desugars to the following type:
        //
        // // the "response"
        // struct {
        //   // the "result"
        //   result @generated_name("P_M_Result") union {
        //     // the "success variant"
        //     response @generated_name("P_M_Response") [user specified response type];
        //     // the "error variant"
        //     err @generated_name("P_M_Error") [user specified error type];
        //   };
        // };
        //
        // Note that this can lead to ambiguity with the success variant, since its member
        // name within the union is "response". The naming convention within fidlc
        // is to refer to each type using the name provided in the comments
        // above (i.e. "response" refers to the top level struct, not the success variant).
        //
        // The naming scheme for the result type and the success variant in a response
        // with an error type predates the design of the anonymous name flattening
        // algorithm, and we therefore they are overridden to be backwards compatible.
        result_context = response_context->EnterMember(GeneratedSimpleName("result"));
        result_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Result"}, "_"));
        success_variant_context = result_context->EnterMember(GeneratedSimpleName("response"));
        success_variant_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Response"}, "_"));
        err_variant_context = result_context->EnterMember(GeneratedSimpleName("err"));
        err_variant_context->set_name_override(
            utils::StringJoin({protocol_name.decl_name(), method_name.data(), "Error"}, "_"));
      }

      // The context for the user specified type within the response part of the method
      // (i.e. `Foo() -> («this source») ...`) is either the top level response context
      // or that of the success variant of the result type
      auto ctx = has_error ? success_variant_context : response_context;
      bool result = std::visit(
          [this, method_name, has_error, ctx, &maybe_response](auto params) -> bool {
            return ConsumeParameterList(method_name, ctx, std::move(params), !has_error,
                                        &maybe_response);
          },
          std::move(method->maybe_response));
      if (!result)
        return;

      if (has_error) {
        assert(err_variant_context != nullptr &&
               "compiler bug: error type contexts should have been computed");
        // we move out of `response_context` only if !has_error, so it's safe to use here
        if (!CreateMethodResult(err_variant_context, response_span, method.get(), maybe_response,
                                &maybe_response))
          return;
      }
    }

    assert(has_request || has_response);
    methods.emplace_back(std::move(attributes), std::move(method->identifier), method_name,
                         has_request, maybe_request, has_response, maybe_response);
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(protocol_declaration->attributes), &attributes)) {
    return;
  }

  RegisterDecl(std::make_unique<Protocol>(std::move(attributes), std::move(protocol_name),
                                          std::move(composed_protocols), std::move(methods)));
}

bool Library::ConsumeParameterList(SourceSpan method_name, std::shared_ptr<NamingContext> context,
                                   std::unique_ptr<raw::ParameterListOld> parameter_list,
                                   bool is_request_or_response, Struct** out_struct_decl) {
  // If is_request_or_response is false, this parameter list is being generated
  // as one of two members in the "Foo_Result" union.  In this case, we proceed
  // with generating an empty struct, so that the first member of this union,
  // "Foo_Response," may be filled.  In the other case, an empty parameter list
  // means that the body payload is expected to be empty, so the out_struct_decl
  // should be left as null to indicate as much.
  if (is_request_or_response && parameter_list->parameter_list.empty()) {
    return true;
  }

  std::vector<Struct::Member> members;
  for (auto& parameter : parameter_list->parameter_list) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(parameter->attributes), &attributes)) {
      return false;
    }

    TypeConstructor type_ctor;
    if (!ConsumeTypeConstructor(std::move(parameter->type_ctor),
                                context->EnterMember(parameter->span()), &type_ctor))
      return false;
    members.emplace_back(std::move(type_ctor), parameter->identifier->span(),
                         /* maybe_default_value=*/nullptr, std::move(attributes));
  }

  if (!RegisterDecl(std::make_unique<Struct>(
          nullptr /* attributes */,
          Name::CreateAnonymous(this, parameter_list->span(), std::move(context)),
          std::move(members), std::nullopt /* resourceness */, is_request_or_response)))
    return false;

  *out_struct_decl = struct_declarations_.back().get();
  struct_declarations_.back()->from_parameter_list_span = parameter_list->span();
  return true;
}

bool Library::ConsumeParameterList(SourceSpan method_name, std::shared_ptr<NamingContext> context,
                                   std::unique_ptr<raw::ParameterListNew> parameter_layout,
                                   bool is_request_or_response, Struct** out_struct_decl) {
  // If is_request_or_response is false, this parameter list is being generated
  // as one of two members in the "Foo_Result" union.  In this case, we proceed
  // with generating an empty struct, so that the first member of this union,
  // "Foo_Response," may be filled.  In the other case, an empty parameter list
  // means that the body payload is expected to be empty, so the out_struct_decl
  // should be left as null to indicate as much.
  if (!parameter_layout->type_ctor) {
    if (!is_request_or_response) {
      Fail(ErrResponsesWithErrorsMustNotBeEmpty, parameter_layout->span(), method_name);
      return false;
    }
    return true;
  }

  Name name = Name::CreateAnonymous(this, parameter_layout->span(), context);
  if (!ConsumeTypeConstructorNew(std::move(parameter_layout->type_ctor), std::move(context),
                                 /*raw_attribute_list=*/nullptr, is_request_or_response,
                                 /*out_type_=*/nullptr))
    return false;

  auto* decl = LookupDeclByName(name);
  if (!decl)
    return false;

  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<Struct*>(decl);
      if (is_request_or_response && struct_decl->members.empty()) {
        Fail(ErrEmptyPayloadStructs, name);
      }
      break;
    }
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum: {
      return Fail(ErrInvalidParameterListType, decl);
    }
    case Decl::Kind::kTable:
    case Decl::Kind::kUnion: {
      return Fail(ErrNotYetSupportedParameterListType, decl);
    }
    default: {
      assert(false && "unexpected decl kind");
    }
  }

  *out_struct_decl = struct_declarations_.back().get();
  return true;
}

bool Library::ConsumeResourceDeclaration(
    std::unique_ptr<raw::ResourceDeclaration> resource_declaration) {
  auto name = Name::CreateSourced(this, resource_declaration->identifier->span());
  std::vector<Resource::Property> properties;
  for (auto& property : resource_declaration->properties) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(property->attributes), &attributes)) {
      return false;
    }

    TypeConstructor type_ctor;
    if (!ConsumeTypeConstructor(std::move(property->type_ctor), NamingContext::Create(name),
                                &type_ctor))
      return false;
    properties.emplace_back(std::move(type_ctor), property->identifier->span(),
                            std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(resource_declaration->attributes), &attributes)) {
    return false;
  }

  TypeConstructor type_ctor;
  if (raw::IsTypeConstructorDefined(resource_declaration->maybe_type_ctor)) {
    if (!ConsumeTypeConstructor(std::move(resource_declaration->maybe_type_ctor),
                                NamingContext::Create(name), &type_ctor))
      return false;
  } else {
    type_ctor = TypeConstructorOld::CreateSizeType();
  }

  return RegisterDecl(std::make_unique<Resource>(std::move(attributes), std::move(name),
                                                 std::move(type_ctor), std::move(properties)));
}

void Library::ConsumeServiceDeclaration(std::unique_ptr<raw::ServiceDeclaration> service_decl) {
  auto name = Name::CreateSourced(this, service_decl->identifier->span());
  auto context = NamingContext::Create(name);
  std::vector<Service::Member> members;
  for (auto& member : service_decl->members) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return;
    }

    TypeConstructor type_ctor;
    if (!ConsumeTypeConstructor(std::move(member->type_ctor), context->EnterMember(member->span()),
                                &type_ctor))
      return;
    members.emplace_back(std::move(type_ctor), member->identifier->span(), std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(service_decl->attributes), &attributes)) {
    return;
  }

  RegisterDecl(
      std::make_unique<Service>(std::move(attributes), std::move(name), std::move(members)));
}

void Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
  auto name = Name::CreateSourced(this, struct_declaration->identifier->span());

  std::vector<Struct::Member> members;
  for (auto& member : struct_declaration->members) {
    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return;
    }

    std::unique_ptr<TypeConstructorOld> type_ctor;
    if (!ConsumeTypeConstructorOld(std::move(member->type_ctor), &type_ctor))
      return;
    std::unique_ptr<Constant> maybe_default_value;
    if (member->maybe_default_value != nullptr) {
      if (!ConsumeConstant(std::move(member->maybe_default_value), &maybe_default_value))
        return;
    }
    members.emplace_back(std::move(type_ctor), member->identifier->span(),
                         std::move(maybe_default_value), std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(struct_declaration->attributes), &attributes)) {
    return;
  }

  RegisterDecl(std::make_unique<Struct>(std::move(attributes), std::move(name), std::move(members),
                                        struct_declaration->resourceness));
}

void Library::ConsumeTableDeclaration(std::unique_ptr<raw::TableDeclaration> table_declaration) {
  auto name = Name::CreateSourced(this, table_declaration->identifier->span());

  std::vector<Table::Member> members;
  for (auto& member : table_declaration->members) {
    auto ordinal_literal = std::move(member->ordinal);

    if (member->maybe_used) {
      std::unique_ptr<AttributeList> attributes;
      if (!ConsumeAttributeList(std::move(member->maybe_used->attributes), &attributes)) {
        return;
      }

      std::unique_ptr<TypeConstructorOld> type_ctor;
      if (!ConsumeTypeConstructorOld(std::move(member->maybe_used->type_ctor), &type_ctor))
        return;
      std::unique_ptr<Constant> maybe_default_value;
      if (member->maybe_used->maybe_default_value) {
        // TODO(fxbug.dev/7932): Support defaults on tables.
        const auto default_value = member->maybe_used->maybe_default_value.get();
        reporter_->Report(ErrDefaultsOnTablesNotSupported, default_value->span());
      }
      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        Fail(ErrNullableTableMember, member->span());
        return;
      }
      members.emplace_back(std::move(ordinal_literal), std::move(type_ctor),
                           member->maybe_used->identifier->span(), std::move(maybe_default_value),
                           std::move(attributes));
    } else {
      members.emplace_back(std::move(ordinal_literal), member->span());
    }
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(table_declaration->attributes), &attributes)) {
    return;
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
      std::unique_ptr<AttributeList> attributes;
      if (!ConsumeAttributeList(std::move(member->maybe_used->attributes), &attributes)) {
        return;
      }

      std::unique_ptr<TypeConstructorOld> type_ctor;
      if (!ConsumeTypeConstructorOld(std::move(member->maybe_used->type_ctor), &type_ctor))
        return;
      if (member->maybe_used->maybe_default_value) {
        const auto default_value = member->maybe_used->maybe_default_value.get();
        reporter_->Report(ErrDefaultsOnUnionsNotSupported, default_value->span());
      }
      if (type_ctor->nullability != types::Nullability::kNonnullable) {
        Fail(ErrNullableUnionMember, member->span());
        return;
      }

      members.emplace_back(std::move(explicit_ordinal), std::move(type_ctor),
                           member->maybe_used->identifier->span(), std::move(attributes));
    } else {
      members.emplace_back(std::move(explicit_ordinal), member->span());
    }
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(union_declaration->attributes), &attributes)) {
    return;
  }

  RegisterDecl(std::make_unique<Union>(std::move(attributes), std::move(name), std::move(members),
                                       union_declaration->strictness,
                                       union_declaration->resourceness));
}

namespace {

// Sets the naming context's generated name override to the @generated_name attribute's value if it
// is present in the input attribute list, or does nothing otherwise.
void MaybeOverrideName(const AttributeList& attributes, NamingContext* context) {
  auto override_attr = attributes.GetAttribute("generated_name");
  if (!override_attr.has_value())
    return;
  auto override_name_arg = override_attr.value().get().GetStandaloneAnonymousArg();
  if (!override_name_arg.has_value())
    return;

  const auto& attr_span = override_name_arg.value().get().value->span;
  assert(attr_span.data().size() > 2 && "expected attribute arg to at least have quotes");
  // remove the quotes from string literal
  context->set_name_override(std::string(attr_span.data().substr(1, attr_span.data().size() - 2)));
}

}  // namespace

// TODO(fxbug.dev/77853): these conversion methods may need to be refactored
//  once the new flat AST lands, and such coercion  is no longer needed.
template <typename T, typename M>
bool Library::ConsumeValueLayout(std::unique_ptr<raw::Layout> layout,
                                 const std::shared_ptr<NamingContext>& context,
                                 std::unique_ptr<raw::AttributeListNew> raw_attribute_list) {
  std::vector<M> members;
  size_t index = 0;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::ValueLayoutMember*>(mem.get());
    auto span = member->identifier->span();

    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return false;
    }

    std::unique_ptr<Constant> value;
    if (!ConsumeConstant(std::move(member->value), &value))
      return false;

    members.emplace_back(span, std::move(value), std::move(attributes));
    index++;
  }

  std::unique_ptr<TypeConstructorNew> subtype_ctor;
  if (layout->subtype_ctor != nullptr) {
    if (!ConsumeTypeConstructorNew(std::move(layout->subtype_ctor), context,
                                   /*raw_attribute_list=*/nullptr, /*is_request_or_response=*/false,
                                   &subtype_ctor))
      return false;
  } else {
    subtype_ctor = TypeConstructorNew::CreateSizeType();
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(raw_attribute_list), &attributes)) {
    return false;
  }

  auto strictness = types::Strictness::kFlexible;
  if (layout->modifiers != nullptr)
    strictness = layout->modifiers->maybe_strictness.value_or(types::Strictness::kFlexible);

  RegisterDecl(std::make_unique<T>(std::move(attributes), context->ToName(this, layout->span()),
                                   std::move(subtype_ctor), std::move(members), strictness));
  return true;
}

template <typename T, typename M>
bool Library::ConsumeOrdinaledLayout(std::unique_ptr<raw::Layout> layout,
                                     const std::shared_ptr<NamingContext>& context,
                                     std::unique_ptr<raw::AttributeListNew> raw_attribute_list) {
  std::vector<M> members;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::OrdinaledLayoutMember*>(mem.get());
    if (member->reserved) {
      members.emplace_back(std::move(member->ordinal), member->span());
      continue;
    }

    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return false;
    }

    std::unique_ptr<TypeConstructorNew> type_ctor;
    if (!ConsumeTypeConstructorNew(
            std::move(member->type_ctor), context->EnterMember(member->identifier->span()),
            /*raw_attribute_list=*/nullptr, /*is_request_or_response=*/false, &type_ctor))
      return false;

    members.emplace_back(std::move(member->ordinal), std::move(type_ctor),
                         member->identifier->span(), std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(raw_attribute_list), &attributes)) {
    return false;
  }
  MaybeOverrideName(*attributes, context.get());

  auto strictness = types::Strictness::kFlexible;
  if (layout->modifiers != nullptr)
    strictness = layout->modifiers->maybe_strictness.value_or(types::Strictness::kFlexible);

  auto resourceness = types::Resourceness::kValue;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_resourceness != std::nullopt)
    resourceness = layout->modifiers->maybe_resourceness.value_or(types::Resourceness::kValue);

  RegisterDecl(std::make_unique<T>(std::move(attributes), context->ToName(this, layout->span()),
                                   std::move(members), strictness, resourceness));
  return true;
}

bool Library::ConsumeStructLayout(std::unique_ptr<raw::Layout> layout,
                                  const std::shared_ptr<NamingContext>& context,
                                  std::unique_ptr<raw::AttributeListNew> raw_attribute_list,
                                  bool is_request_or_response) {
  std::vector<Struct::Member> members;
  for (auto& mem : layout->members) {
    auto member = static_cast<raw::StructLayoutMember*>(mem.get());

    std::unique_ptr<AttributeList> attributes;
    if (!ConsumeAttributeList(std::move(member->attributes), &attributes)) {
      return false;
    }

    std::unique_ptr<TypeConstructorNew> type_ctor;
    if (!ConsumeTypeConstructorNew(
            std::move(member->type_ctor), context->EnterMember(member->identifier->span()),
            /*raw_attribute_list=*/nullptr, /*is_request_or_response=*/false, &type_ctor))
      return false;

    std::unique_ptr<Constant> default_value;
    if (member->default_value != nullptr) {
      ConsumeConstant(std::move(member->default_value), &default_value);
    }

    members.emplace_back(std::move(type_ctor), member->identifier->span(), std::move(default_value),
                         std::move(attributes));
  }

  std::unique_ptr<AttributeList> attributes;
  if (!ConsumeAttributeList(std::move(raw_attribute_list), &attributes)) {
    return false;
  }
  MaybeOverrideName(*attributes, context.get());

  auto resourceness = types::Resourceness::kValue;
  if (layout->modifiers != nullptr && layout->modifiers->maybe_resourceness != std::nullopt)
    resourceness = layout->modifiers->maybe_resourceness.value_or(types::Resourceness::kValue);

  RegisterDecl(std::make_unique<Struct>(std::move(attributes),
                                        context->ToName(this, layout->span()), std::move(members),
                                        resourceness, is_request_or_response));
  return true;
}

bool Library::ConsumeLayout(std::unique_ptr<raw::Layout> layout,
                            const std::shared_ptr<NamingContext>& context,
                            std::unique_ptr<raw::AttributeListNew> raw_attribute_list,
                            bool is_request_or_response) {
  switch (layout->kind) {
    case raw::Layout::Kind::kBits: {
      return ConsumeValueLayout<Bits, Bits::Member>(std::move(layout), context,
                                                    std::move(raw_attribute_list));
    }
    case raw::Layout::Kind::kEnum: {
      return ConsumeValueLayout<Enum, Enum::Member>(std::move(layout), context,
                                                    std::move(raw_attribute_list));
    }
    case raw::Layout::Kind::kStruct: {
      return ConsumeStructLayout(std::move(layout), context, std::move(raw_attribute_list),
                                 is_request_or_response);
    }
    case raw::Layout::Kind::kTable: {
      return ConsumeOrdinaledLayout<Table, Table::Member>(std::move(layout), context,
                                                          std::move(raw_attribute_list));
    }
    case raw::Layout::Kind::kUnion: {
      return ConsumeOrdinaledLayout<Union, Union::Member>(std::move(layout), context,
                                                          std::move(raw_attribute_list));
    }
  }
  assert(false && "layouts must be of type bits, enum, struct, table, or union");
  return true;
}

bool Library::ConsumeTypeConstructorNew(std::unique_ptr<raw::TypeConstructorNew> raw_type_ctor,
                                        const std::shared_ptr<NamingContext>& context,
                                        std::unique_ptr<raw::AttributeListNew> raw_attribute_list,
                                        bool is_request_or_response,
                                        std::unique_ptr<TypeConstructorNew>* out_type_ctor) {
  std::vector<std::unique_ptr<LayoutParameter>> params;
  std::optional<SourceSpan> params_span;
  if (raw_type_ctor->parameters) {
    params_span = raw_type_ctor->parameters->span();
    for (auto& p : raw_type_ctor->parameters->items) {
      auto param = std::move(p);
      auto span = param->span();
      switch (param->kind) {
        case raw::LayoutParameter::Kind::kLiteral: {
          auto literal_param = static_cast<raw::LiteralLayoutParameter*>(param.get());
          std::unique_ptr<LiteralConstant> constant;
          ConsumeLiteralConstant(literal_param->literal.get(), &constant);

          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<LiteralLayoutParameter>(std::move(constant), span);
          params.push_back(std::move(consumed));
          break;
        }
        case raw::LayoutParameter::Kind::kType: {
          auto type_param = static_cast<raw::TypeLayoutParameter*>(param.get());
          std::unique_ptr<TypeConstructorNew> type_ctor;
          if (!ConsumeTypeConstructorNew(std::move(type_param->type_ctor), context,
                                         /*raw_attribute_list=*/nullptr, is_request_or_response,
                                         &type_ctor))
            return false;

          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<TypeLayoutParameter>(std::move(type_ctor), span);
          params.push_back(std::move(consumed));
          break;
        }
        case raw::LayoutParameter::Kind::kIdentifier: {
          auto id_param = static_cast<raw::IdentifierLayoutParameter*>(param.get());
          auto name = CompileCompoundIdentifier(id_param->identifier.get());
          if (!name)
            return false;

          std::unique_ptr<LayoutParameter> consumed =
              std::make_unique<IdentifierLayoutParameter>(std::move(name.value()), span);
          params.push_back(std::move(consumed));
          break;
        }
      }
    }
  }

  std::vector<std::unique_ptr<Constant>> constraints;
  std::optional<SourceSpan> constraints_span;
  if (raw_type_ctor->constraints) {
    constraints_span = raw_type_ctor->constraints->span();
    for (auto& c : raw_type_ctor->constraints->items) {
      std::unique_ptr<Constant> constraint;
      if (!ConsumeConstant(std::move(c), &constraint))
        return false;
      constraints.push_back(std::move(constraint));
    }
  }

  if (raw_type_ctor->layout_ref->kind == raw::LayoutReference::Kind::kInline) {
    auto inline_ref = static_cast<raw::InlineLayoutReference*>(raw_type_ctor->layout_ref.get());
    auto attributes = std::move(raw_attribute_list);
    if (inline_ref->attributes != nullptr)
      attributes = std::move(inline_ref->attributes);
    if (!ConsumeLayout(std::move(inline_ref->layout), context, std::move(attributes),
                       is_request_or_response))
      return false;

    if (out_type_ctor)
      *out_type_ctor = std::make_unique<TypeConstructorNew>(
          context->ToName(this, raw_type_ctor->layout_ref->span()),
          std::make_unique<LayoutParameterList>(std::move(params), params_span),
          std::make_unique<TypeConstraints>(std::move(constraints), constraints_span));
    return true;
  }

  // TODO(fxbug.dev/76349): named parameter lists are not yet allowed, so we
  //  need to ensure that is_request_or_response is false at this point.  Once
  //  that feature is enabled, this check can be removed.
  if (is_request_or_response) {
    return Fail(ErrNamedParameterListTypesNotYetSupported, raw_type_ctor->span());
  }

  auto named_ref = static_cast<raw::NamedLayoutReference*>(raw_type_ctor->layout_ref.get());
  auto name = CompileCompoundIdentifier(named_ref->identifier.get());
  if (!name)
    return false;

  assert(out_type_ctor && "out type ctors should always be provided for a named type ctor");
  *out_type_ctor = std::make_unique<TypeConstructorNew>(
      std::move(name.value()),
      std::make_unique<LayoutParameterList>(std::move(params), params_span),
      std::make_unique<TypeConstraints>(std::move(constraints), constraints_span));
  return true;
}

bool Library::ConsumeTypeConstructor(raw::TypeConstructor raw_type_ctor,
                                     const std::shared_ptr<NamingContext>& context,
                                     TypeConstructor* out_type) {
  return std::visit(fidl::utils::matchers{
                        [&, this](std::unique_ptr<raw::TypeConstructorOld> e) -> bool {
                          std::unique_ptr<TypeConstructorOld> out;
                          bool result = ConsumeTypeConstructorOld(std::move(e), &out);
                          *out_type = std::move(out);
                          return result;
                        },
                        [&, this](std::unique_ptr<raw::TypeConstructorNew> e) -> bool {
                          std::unique_ptr<TypeConstructorNew> out;
                          bool result = ConsumeTypeConstructorNew(
                              std::move(e), context, /*raw_attribute_list=*/nullptr,
                              /*is_request_or_response=*/false, &out);
                          *out_type = std::move(out);
                          return result;
                        },
                    },
                    std::move(raw_type_ctor));
}

void Library::ConsumeTypeDecl(std::unique_ptr<raw::TypeDecl> type_decl) {
  auto name = Name::CreateSourced(this, type_decl->identifier->span());
  auto& layout_ref = type_decl->type_ctor->layout_ref;
  // TODO(fxbug.dev/7807)
  if (layout_ref->kind == raw::LayoutReference::Kind::kNamed) {
    auto named_ref = static_cast<raw::NamedLayoutReference*>(layout_ref.get());
    Fail(ErrNewTypesNotAllowed, type_decl->span(), name, named_ref->span().data());
    return;
  }

  ConsumeTypeConstructorNew(std::move(type_decl->type_ctor), NamingContext::Create(name),
                            std::move(type_decl->attributes),
                            /*is_request_or_response=*/false,
                            /*out_type=*/nullptr);
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
  if (raw::IsAttributeListDefined(file->library_decl->attributes)) {
    std::unique_ptr<AttributeList> consumed_attributes;
    if (!ConsumeAttributeList(std::move(file->library_decl->attributes), &consumed_attributes)) {
      return false;
    }

    ValidateAttributesPlacement(this);
    if (!attributes) {
      attributes = std::move(consumed_attributes);
    } else {
      AttributesBuilder attributes_builder(reporter_, std::move(attributes->attributes));
      for (auto& attribute : consumed_attributes->attributes) {
        if (!attributes_builder.Insert(std::move(attribute)))
          return false;
      }
      attributes = std::make_unique<AttributeList>(attributes_builder.Done());
    }
  }

  // All fidl files in a library should agree on the library name.
  std::vector<std::string_view> new_name;
  for (const auto& part : file->library_decl->path->components) {
    new_name.push_back(part->span().data());
  }
  if (!library_name_.empty()) {
    if (new_name != library_name_) {
      return Fail(ErrFilesDisagreeOnLibraryName, file->library_decl->path->components[0]->span());
    }
  } else {
    library_name_ = new_name;
  }

  auto step = StartConsumeStep(file->syntax);

  auto using_list = std::move(file->using_list);
  for (auto& using_directive : using_list) {
    step.ForUsing(std::move(using_directive));
  }

  auto alias_list = std::move(file->alias_list);
  for (auto& alias_declaration : alias_list) {
    step.ForAliasDeclaration(std::move(alias_declaration));
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

  auto type_decls = std::move(file->type_decls);
  for (auto& type_decl : type_decls) {
    step.ForTypeDecl(std::move(type_decl));
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

bool Library::TryResolveConstant(Constant* constant, const Type* type) {
  reporter::Reporter::ScopedReportingMode silenced =
      reporter_->OverrideMode(reporter::Reporter::ReportingMode::kDoNotReport);
  return ResolveConstant(constant, type);
}

bool Library::ResolveConstant(Constant* constant, const Type* type) {
  assert(constant != nullptr);

  // Prevent re-entry.
  if (constant->compiled)
    return constant->IsResolved();

  switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
      auto identifier_constant = static_cast<IdentifierConstant*>(constant);
      if (!ResolveIdentifierConstant(identifier_constant, type)) {
        return false;
      }
      break;
    }
    case Constant::Kind::kLiteral: {
      auto literal_constant = static_cast<LiteralConstant*>(constant);
      if (!ResolveLiteralConstant(literal_constant, type)) {
        return false;
      }
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
          if (!ResolveOrOperatorConstant(constant, type,
                                         binary_operator_constant->left_operand->Value(),
                                         binary_operator_constant->right_operand->Value())) {
            return false;
          }
          break;
        }
        default:
          assert(false && "Compiler bug: unhandled binary operator");
      }
      break;
    }
  }

  constant->compiled = true;
  return true;
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

  const Type* const_type = nullptr;
  const ConstantValue* const_val = nullptr;
  switch (decl->kind) {
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<Const*>(decl);
      const_type = GetType(const_decl->type_ctor);
      const_val = &const_decl->value->Value();
      break;
    }
    case Decl::Kind::kEnum: {
      // If there is no member name, fallthrough to default.
      if (auto member_name = identifier_constant->name.member_name(); member_name) {
        auto enum_decl = static_cast<Enum*>(decl);
        const_type = GetType(enum_decl->subtype_ctor);
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
        const_type = GetType(bits_decl->subtype_ctor);
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
                  identifier_constant->name);
    }
  }

  assert(const_val && "Compiler bug: did not set const_val");
  assert(const_type && "Compiler bug: did not set const_type");

  std::unique_ptr<ConstantValue> resolved_val;
  switch (type->kind) {
    case Type::Kind::kString: {
      if (!TypeIsConvertibleTo(const_type, type))
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
          assert(GetType(enum_decl->subtype_ctor)->kind == Type::Kind::kPrimitive);
          primitive_type = static_cast<const PrimitiveType*>(GetType(enum_decl->subtype_ctor));
          break;
        }
        case Decl::Kind::kBits: {
          auto bits_decl = static_cast<const Bits*>(identifier_type->type_decl);
          assert(GetType(bits_decl->subtype_ctor)->kind == Type::Kind::kPrimitive);
          primitive_type = static_cast<const PrimitiveType*>(GetType(bits_decl->subtype_ctor));
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
          if (const_type->name != identifier_type->type_decl->name)
            return fail_with_mismatched_type(const_type->name);
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
  return Fail(ErrCannotConvertConstantToType, identifier_constant, const_type, type);
}

bool Library::ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type) {
  switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kDocComment: {
      auto doc_comment_literal =
          static_cast<raw::DocCommentLiteral*>(literal_constant->literal.get());

      literal_constant->ResolveTo(
          std::make_unique<DocCommentConstantValue>(doc_comment_literal->span().data()));
      return true;
    }
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
      uint64_t string_size = string_data.size() < 2 ? 0 : string_data.size() - 2;
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

bool Library::ResolveAsOptional(Constant* constant) const {
  assert(constant);

  if (constant->kind != Constant::Kind::kIdentifier)
    return false;

  // This refers to the `optional` constraint only if it is "optional" AND
  // it is not shadowed by a previous definition.
  // Note that as we improve scoping rules, we would need to allow `fidl.optional`
  // to be the FQN for the `optional` constant.
  auto identifier_constant = static_cast<IdentifierConstant*>(constant);
  auto decl = LookupDeclByName(identifier_constant->name.memberless_key());
  if (decl)
    return false;

  return identifier_constant->name.decl_name() == "optional";
}

bool Library::CompileAttributeList(AttributeList* attributes) {
  bool ok = true;
  if (attributes && !attributes->attributes.empty()) {
    for (auto& attribute : attributes->attributes) {
      auto schema =
          all_libraries_->RetrieveAttributeSchema(reporter_, attribute, attribute->syntax, true);

      // Check for duplicate args, and return early if we find them.
      std::set<std::string> seen;
      for (const auto& arg : attribute->args) {
        if (arg->name.has_value() && !seen.insert(utils::canonicalize(arg->name.value())).second) {
          ok =
              Fail(ErrDuplicateAttributeArg, attribute->span(), attribute.get(), arg->name.value());
          continue;
        }
      }

      // If we have a schema, resolve each argument based on its expected schema-derived type.
      if (schema != nullptr && !schema->IsDeprecated()) {
        ok = schema->ValidateArgs(reporter_, attribute) ? schema->ResolveArgs(this, attribute)
                                                        : false;
        continue;
      }

      // Schemaless (ie, user defined) attributes must not have numeric arguments.  Resolve all of
      // their arguments, making sure to error on numerics (since those cannot be resolved to the
      // appropriate fidelity).
      for (const auto& arg : attribute->args) {
        static const auto max_size = Size::Max();
        static const StringType kUnboundedStringType = StringType(
            Name::CreateIntrinsic("string"), &max_size, types::Nullability::kNonnullable);
        static const auto kBoolType =
            PrimitiveType(Name::CreateIntrinsic("bool"), types::PrimitiveSubtype::kBool);
        assert(arg->value->kind != Constant::Kind::kBinaryOperator &&
               "attribute arg starting with a binary operator is a parse error");

        // Try first as a string...
        if (!TryResolveConstant(arg->value.get(), &kUnboundedStringType)) {
          // ...then as a bool if that doesn't work.
          if (!TryResolveConstant(arg->value.get(), &kBoolType)) {
            // Since we cannot have an IdentifierConstant resolving to a kDocComment-kinded value,
            // we know that it must resolve to a numeric instead.
            ok = Fail(ErrCannotUseNumericArgsOnCustomAttributes, attribute->span(), arg.get(),
                      attribute.get());
          }
        }
      }
      if (!ok) {
        continue;
      }

      if (attribute->args.size() == 1) {
        attribute->args[0]->name = "value";
      }
      attribute->resolved = true;
    }
  }
  return ok;
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
      return GetType(static_cast<const Bits*>(decl)->subtype_ctor);
    case Decl::Kind::kEnum:
      return GetType(static_cast<const Enum*>(decl)->subtype_ctor);
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

bool Library::AddConstantDependencies(const Constant* constant, std::set<const Decl*>* out_edges) {
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
    case Constant::Kind::kLiteral: {
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
bool Library::DeclDependencies(const Decl* decl, std::set<const Decl*>* out_edges) {
  std::set<const Decl*> edges;

  auto maybe_add_decl = [&edges](TypeConstructorPtr type_ctor) {
    TypeConstructorPtr current = type_ctor;
    for (;;) {
      const auto& invocation = GetLayoutInvocation(current);
      if (invocation.from_type_alias) {
        assert(!invocation.element_type_resolved &&
               "Compiler bug: partial aliases should be disallowed");
        edges.insert(invocation.from_type_alias);
        return;
      }

      const Type* type = GetType(current);
      if (type->nullability == types::Nullability::kNullable)
        return;

      switch (type->kind) {
        case Type::Kind::kHandle: {
          auto handle_type = static_cast<const HandleType*>(type);
          assert(handle_type->resource_decl);
          auto decl = static_cast<const Decl*>(handle_type->resource_decl);
          edges.insert(decl);
          return;
        }
        case Type::Kind::kPrimitive:
        case Type::Kind::kString:
        case Type::Kind::kRequestHandle:
        case Type::Kind::kTransportSide:
        case Type::Kind::kBox:
          return;
        case Type::Kind::kArray:
        case Type::Kind::kVector: {
          if (IsTypeConstructorDefined(invocation.element_type_raw)) {
            current = invocation.element_type_raw;
            break;
          }
          // The type_ctor won't have an arg_type_ctor if the type is Bytes.
          // In that case, just return since there are no edges
          return;
        }
        case Type::Kind::kIdentifier: {
          // should have been caught above and returned early.
          assert(type->nullability != types::Nullability::kNullable);
          auto identifier_type = static_cast<const IdentifierType*>(type);
          auto decl = static_cast<const Decl*>(identifier_type->type_decl);
          if (decl->kind != Decl::Kind::kProtocol) {
            edges.insert(decl);
          }
          return;
        }
      }
    }
  };

  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_decl = static_cast<const Bits*>(decl);
      maybe_add_decl(GetTypeCtorAsPtr(bits_decl->subtype_ctor));
      for (const auto& member : bits_decl->members) {
        if (!AddConstantDependencies(member.value.get(), &edges)) {
          return false;
        }
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      maybe_add_decl(GetTypeCtorAsPtr(const_decl->type_ctor));
      if (!AddConstantDependencies(const_decl->value.get(), &edges)) {
        return false;
      }
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_decl = static_cast<const Enum*>(decl);
      maybe_add_decl(GetTypeCtorAsPtr(enum_decl->subtype_ctor));
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
        if (auto type_decl = LookupDeclByName(composed_protocol.name); type_decl) {
          edges.insert(type_decl);
        }
      }
      for (const auto& method : protocol_decl->methods) {
        if (method.maybe_request_payload != nullptr) {
          edges.insert(method.maybe_request_payload);
        }
        if (method.maybe_response_payload != nullptr) {
          edges.insert(method.maybe_response_payload);
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_decl = static_cast<const Resource*>(decl);
      maybe_add_decl(GetTypeCtorAsPtr(resource_decl->subtype_ctor));
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<const Service*>(decl);
      for (const auto& member : service_decl->members) {
        maybe_add_decl(GetTypeCtorAsPtr(member.type_ctor));
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(decl);
      for (const auto& member : struct_decl->members) {
        maybe_add_decl(GetTypeCtorAsPtr(member.type_ctor));
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
        maybe_add_decl(GetTypeCtorAsPtr(member.maybe_used->type_ctor));
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
        maybe_add_decl(GetTypeCtorAsPtr(member.maybe_used->type_ctor));
      }
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_decl = static_cast<const TypeAlias*>(decl);
      maybe_add_decl(GetTypeCtorAsPtr(type_alias_decl->partial_type_ctor));
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
  std::map<const Decl*, uint32_t, CmpDeclInLibrary> degrees;
  // |inverse_dependencies| records the decls that depend on each decl.
  std::map<const Decl*, std::vector<const Decl*>, CmpDeclInLibrary> inverse_dependencies;
  for (const auto& name_and_decl : declarations_) {
    const Decl* decl = name_and_decl.second;
    std::set<const Decl*> deps;
    if (!DeclDependencies(decl, &deps))
      return false;
    degrees[decl] = static_cast<uint32_t>(deps.size());
    for (const Decl* dep : deps) {
      inverse_dependencies[dep].push_back(decl);
    }
  }

  // Start with all decls that have no incoming edges.
  std::vector<const Decl*> decls_without_deps;
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
    for (const Decl* inverse_dep : inverse_deps) {
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
  if (decl->compiling)
    return Fail(ErrIncludeCycle);
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
      if (struct_decl->from_parameter_list_span) {
        bool value = *struct_decl->resourceness == types::Resourceness::kResource;
        derived_resourceness.insert({struct_decl->from_parameter_list_span->ToKey(), value});
      }
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

void Library::VerifyDeclAttributes(const Decl* decl) {
  assert(decl->compiled && "verification must happen after compilation of decls");
  auto placement_ok = reporter_->Checkpoint();
  switch (decl->kind) {
    case Decl::Kind::kBits: {
      auto bits_declaration = static_cast<const Bits*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(bits_declaration);
      for (const auto& member : bits_declaration->members) {
        ValidateAttributesPlacement(&member);
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(bits_declaration);
      }
      break;
    }
    case Decl::Kind::kConst: {
      auto const_decl = static_cast<const Const*>(decl);
      // Attributes: for const declarations, we only check placement.
      ValidateAttributesPlacement(const_decl);
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(const_decl);
      }
      break;
    }
    case Decl::Kind::kEnum: {
      auto enum_declaration = static_cast<const Enum*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(enum_declaration);
      for (const auto& member : enum_declaration->members) {
        ValidateAttributesPlacement(&member);
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(enum_declaration);
      }
      break;
    }
    case Decl::Kind::kProtocol: {
      auto protocol_declaration = static_cast<const Protocol*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(protocol_declaration);
      for (const auto& composed_protocol : protocol_declaration->composed_protocols) {
        ValidateAttributesPlacement(&composed_protocol);
      }
      for (const auto& method_with_info : protocol_declaration->all_methods) {
        ValidateAttributesPlacement(method_with_info.method);
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        for (const auto method_with_info : protocol_declaration->all_methods) {
          const auto& method = *method_with_info.method;
          // All of the attributes on the protocol get checked against each of
          // its methods as well.
          ValidateAttributesConstraints(&method, protocol_declaration->attributes.get());
          ValidateAttributesConstraints(&method);
        }
      }
      break;
    }
    case Decl::Kind::kResource: {
      auto resource_declaration = static_cast<const Resource*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(resource_declaration);
      for (const auto& property : resource_declaration->properties) {
        ValidateAttributesPlacement(&property);
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(resource_declaration);
      }
      break;
    }
    case Decl::Kind::kService: {
      auto service_decl = static_cast<const Service*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(service_decl);
      for (const auto& member : service_decl->members) {
        ValidateAttributesPlacement(&member);
      }
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraint.
        ValidateAttributesConstraints(service_decl);
      }
      break;
    }
    case Decl::Kind::kStruct: {
      auto struct_declaration = static_cast<const Struct*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(struct_declaration);
      for (const auto& member : struct_declaration->members) {
        ValidateAttributesPlacement(&member);
      }
      if (placement_ok.NoNewErrors()) {
        for (const auto& member : struct_declaration->members) {
          ValidateAttributesConstraints(&member);
        }
        // Attributes: check constraint.
        ValidateAttributesConstraints(struct_declaration);
      }
      break;
    }
    case Decl::Kind::kTable: {
      auto table_declaration = static_cast<const Table*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(table_declaration);
      for (const auto& member : table_declaration->members) {
        if (!member.maybe_used)
          continue;
        ValidateAttributesPlacement(member.maybe_used.get());
      }
      if (placement_ok.NoNewErrors()) {
        for (const auto& member : table_declaration->members) {
          if (!member.maybe_used)
            continue;
          ValidateAttributesConstraints(member.maybe_used.get());
        }
        // Attributes: check constraint.
        ValidateAttributesConstraints(table_declaration);
      }
      break;
    }
    case Decl::Kind::kUnion: {
      auto union_declaration = static_cast<const Union*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(union_declaration);
      for (const auto& member : union_declaration->members) {
        if (!member.maybe_used)
          continue;
        ValidateAttributesPlacement(member.maybe_used.get());
      }
      if (placement_ok.NoNewErrors()) {
        for (const auto& member : union_declaration->members) {
          if (!member.maybe_used)
            continue;
          ValidateAttributesConstraints(member.maybe_used.get());
        }
        // Attributes: check constraint.
        ValidateAttributesConstraints(union_declaration);
      }
      break;
    }
    case Decl::Kind::kTypeAlias: {
      auto type_alias_declaration = static_cast<const TypeAlias*>(decl);
      // Attributes: check placement.
      ValidateAttributesPlacement(type_alias_declaration);
      if (placement_ok.NoNewErrors()) {
        // Attributes: check constraints.
        ValidateAttributesConstraints(type_alias_declaration);
      }
      break;
    }
  }  // switch
}

void VerifyResourcenessStep::ForDecl(const Decl* decl) {
  assert(decl->compiled && "verification must happen after compilation of decls");
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      const auto* struct_decl = static_cast<const Struct*>(decl);
      if (struct_decl->resourceness == types::Resourceness::kValue) {
        for (const auto& member : struct_decl->members) {
          if (EffectiveResourceness(GetType(member.type_ctor)) == types::Resourceness::kResource) {
            library_->reporter_->Report(ErrTypeMustBeResource, struct_decl->name.span(),
                                        struct_decl->name, member.name.data(),
                                        std::string_view("struct"), struct_decl->name);
          }
        }
      }
      break;
    }
    case Decl::Kind::kTable: {
      const auto* table_decl = static_cast<const Table*>(decl);
      if (table_decl->resourceness == types::Resourceness::kValue) {
        for (const auto& member : table_decl->members) {
          if (member.maybe_used) {
            const auto& used = *member.maybe_used;
            if (EffectiveResourceness(GetType(used.type_ctor)) == types::Resourceness::kResource) {
              library_->reporter_->Report(ErrTypeMustBeResource, table_decl->name.span(),
                                          table_decl->name, used.name.data(),
                                          std::string_view("table"), table_decl->name);
            }
          }
        }
      }
      break;
    }
    case Decl::Kind::kUnion: {
      const auto* union_decl = static_cast<const Union*>(decl);
      if (union_decl->resourceness == types::Resourceness::kValue) {
        for (const auto& member : union_decl->members) {
          if (member.maybe_used) {
            const auto& used = *member.maybe_used;
            if (EffectiveResourceness(GetType(used.type_ctor)) == types::Resourceness::kResource) {
              library_->reporter_->Report(ErrTypeMustBeResource, union_decl->name.span(),
                                          union_decl->name, used.name.data(),
                                          std::string_view("union"), union_decl->name);
            }
          }
        }
      }
      break;
    }
    default:
      break;
  }
}

types::Resourceness Type::Resourceness() const {
  switch (this->kind) {
    case Type::Kind::kPrimitive:
    case Type::Kind::kString:
      return types::Resourceness::kValue;
    case Type::Kind::kHandle:
    case Type::Kind::kRequestHandle:
    case Type::Kind::kTransportSide:
      return types::Resourceness::kResource;
    case Type::Kind::kArray:
      return static_cast<const ArrayType*>(this)->element_type->Resourceness();
    case Type::Kind::kVector:
      return static_cast<const VectorType*>(this)->element_type->Resourceness();
    case Type::Kind::kIdentifier:
      break;
    case Type::Kind::kBox:
      return static_cast<const BoxType*>(this)->boxed_type->Resourceness();
  }

  const auto* decl = static_cast<const IdentifierType*>(this)->type_decl;

  switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
      return types::Resourceness::kValue;
    case Decl::Kind::kProtocol:
      return types::Resourceness::kResource;
    case Decl::Kind::kStruct:
      assert(decl->compiled && "Compiler bug: accessing resourceness of not-yet-compiled struct");
      return static_cast<const Struct*>(decl)->resourceness.value();
    case Decl::Kind::kTable:
      return static_cast<const Table*>(decl)->resourceness;
    case Decl::Kind::kUnion:
      assert(decl->compiled && "Compiler bug: accessing resourceness of not-yet-compiled union");
      return static_cast<const Union*>(decl)->resourceness.value();
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
    case Decl::Kind::kService:
    case Decl::Kind::kTypeAlias:
      assert(false && "Compiler bug: unexpected kind");
  }

  __builtin_unreachable();
}

types::Resourceness VerifyResourcenessStep::EffectiveResourceness(const Type* type) {
  switch (type->kind) {
    case Type::Kind::kPrimitive:
    case Type::Kind::kString:
      return types::Resourceness::kValue;
    case Type::Kind::kHandle:
    case Type::Kind::kRequestHandle:
    case Type::Kind::kTransportSide:
      return types::Resourceness::kResource;
    case Type::Kind::kArray:
      return EffectiveResourceness(static_cast<const ArrayType*>(type)->element_type);
    case Type::Kind::kVector:
      return EffectiveResourceness(static_cast<const VectorType*>(type)->element_type);
    case Type::Kind::kIdentifier:
      break;
    case Type::Kind::kBox:
      return EffectiveResourceness(static_cast<const BoxType*>(type)->boxed_type);
  }

  const auto* decl = static_cast<const IdentifierType*>(type)->type_decl;

  switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
      return types::Resourceness::kValue;
    case Decl::Kind::kProtocol:
      return types::Resourceness::kResource;
    case Decl::Kind::kStruct:
      if (static_cast<const Struct*>(decl)->resourceness.value() ==
          types::Resourceness::kResource) {
        return types::Resourceness::kResource;
      }
      break;
    case Decl::Kind::kTable:
      if (static_cast<const Table*>(decl)->resourceness == types::Resourceness::kResource) {
        return types::Resourceness::kResource;
      }
      break;
    case Decl::Kind::kUnion:
      if (static_cast<const Union*>(decl)->resourceness.value() == types::Resourceness::kResource) {
        return types::Resourceness::kResource;
      }
      break;
    case Decl::Kind::kService:
      return types::Resourceness::kValue;
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
    case Decl::Kind::kTypeAlias:
      assert(false && "Compiler bug: unexpected kind");
  }

  const auto [it, inserted] = effective_resourceness_.try_emplace(decl, std::nullopt);
  if (!inserted) {
    const auto& maybe_value = it->second;
    // If we already computed effective resourceness, return it. If we started
    // computing it but did not complete (nullopt), we're in a cycle, so return
    // kValue as the default assumption.
    return maybe_value.value_or(types::Resourceness::kValue);
  }

  switch (decl->kind) {
    case Decl::Kind::kStruct:
      for (const auto& member : static_cast<const Struct*>(decl)->members) {
        if (EffectiveResourceness(GetType(member.type_ctor)) == types::Resourceness::kResource) {
          effective_resourceness_[decl] = types::Resourceness::kResource;
          return types::Resourceness::kResource;
        }
      }
      break;
    case Decl::Kind::kTable:
      for (const auto& member : static_cast<const Table*>(decl)->members) {
        const auto& used = member.maybe_used;
        if (used &&
            EffectiveResourceness(GetType(used->type_ctor)) == types::Resourceness::kResource) {
          effective_resourceness_[decl] = types::Resourceness::kResource;
          return types::Resourceness::kResource;
        }
      }
      break;
    case Decl::Kind::kUnion:
      for (const auto& member : static_cast<const Union*>(decl)->members) {
        const auto& used = member.maybe_used;
        if (used &&
            EffectiveResourceness(GetType(used->type_ctor)) == types::Resourceness::kResource) {
          effective_resourceness_[decl] = types::Resourceness::kResource;
          return types::Resourceness::kResource;
        }
      }
      break;
    default:
      assert(false && "Compiler bug: unexpected kind");
  }

  effective_resourceness_[decl] = types::Resourceness::kValue;
  return types::Resourceness::kValue;
}

bool Library::CompileBits(Bits* bits_declaration) {
  if (!CompileAttributeList(bits_declaration->attributes.get())) {
    return false;
  }
  for (auto& member : bits_declaration->members) {
    if (!CompileAttributeList(member.attributes.get())) {
      return false;
    }
  }

  if (!CompileTypeConstructor(&bits_declaration->subtype_ctor))
    return false;

  if (GetType(bits_declaration->subtype_ctor)->kind != Type::Kind::kPrimitive) {
    return Fail(ErrBitsTypeMustBeUnsignedIntegralPrimitive, *bits_declaration,
                GetType(bits_declaration->subtype_ctor));
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(GetType(bits_declaration->subtype_ctor));
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
                  GetType(bits_declaration->subtype_ctor));
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
  if (!CompileAttributeList(const_declaration->attributes.get())) {
    return false;
  }

  if (!CompileTypeConstructor(&const_declaration->type_ctor))
    return false;
  const auto* const_type = GetType(const_declaration->type_ctor);
  if (!TypeCanBeConst(const_type)) {
    return Fail(ErrInvalidConstantType, *const_declaration, const_type);
  }
  if (!ResolveConstant(const_declaration->value.get(), const_type))
    return Fail(ErrCannotResolveConstantValue, *const_declaration);

  return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
  if (!CompileAttributeList(enum_declaration->attributes.get())) {
    return false;
  }
  for (auto& member : enum_declaration->members) {
    if (!CompileAttributeList(member.attributes.get())) {
      return false;
    }
  }

  if (!CompileTypeConstructor(&enum_declaration->subtype_ctor))
    return false;

  if (GetType(enum_declaration->subtype_ctor)->kind != Type::Kind::kPrimitive) {
    return Fail(ErrEnumTypeMustBeIntegralPrimitive, *enum_declaration,
                GetType(enum_declaration->subtype_ctor));
  }

  // Validate constants.
  auto primitive_type = static_cast<const PrimitiveType*>(GetType(enum_declaration->subtype_ctor));
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
                  GetType(enum_declaration->subtype_ctor));
  }

  return true;
}

bool HasSimpleLayout(const Decl* decl) { return decl->HasAttribute("for_deprecated_c_bindings"); }

bool Library::CompileResource(Resource* resource_declaration) {
  Scope<std::string_view> scope;

  if (!CompileAttributeList(resource_declaration->attributes.get())) {
    return false;
  }

  if (!CompileTypeConstructor(&resource_declaration->subtype_ctor))
    return false;

  if (GetType(resource_declaration->subtype_ctor)->kind != Type::Kind::kPrimitive) {
    return Fail(ErrEnumTypeMustBeIntegralPrimitive, *resource_declaration,
                GetType(resource_declaration->subtype_ctor));
  }

  for (auto& property : resource_declaration->properties) {
    if (!CompileAttributeList(property.attributes.get())) {
      return false;
    }

    auto name_result = scope.Insert(property.name.data(), property.name);
    if (!name_result.ok())
      return Fail(ErrDuplicateResourcePropertyName, property.name,
                  name_result.previous_occurrence());

    if (!CompileTypeConstructor(&property.type_ctor))
      return false;
  }
  return true;
}

bool Library::CompileProtocol(Protocol* protocol_declaration) {
  if (!CompileAttributeList(protocol_declaration->attributes.get())) {
    return false;
  }

  MethodScope method_scope;
  auto CheckScopes = [this, &protocol_declaration, &method_scope](const Protocol* protocol,
                                                                  auto Visitor) -> bool {
    for (const auto& composed_protocol : protocol->composed_protocols) {
      auto name = composed_protocol.name;
      auto decl = LookupDeclByName(name);
      // TODO(fxbug.dev/7926): Special handling here should not be required, we
      // should first rely on creating the types representing composed
      // protocols.
      if (!decl) {
        return Fail(ErrUnknownType, name, name);
      }
      if (decl->kind != Decl::Kind::kProtocol)
        return Fail(ErrComposingNonProtocol, name);
      auto composed_protocol_declaration = static_cast<const Protocol*>(decl);
      auto span = composed_protocol_declaration->name.span();
      assert(span);
      if (method_scope.protocols.Insert(composed_protocol_declaration, span.value()).ok()) {
        if (!Visitor(composed_protocol_declaration, Visitor))
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

  // Before scope checking can occur, ordinals must be generated for each of the
  // protocol's methods, including those that were composed from transitive
  // child protocols.  This means that child protocols must be compiled prior to
  // this one, or they will not have generated_ordinal64s on their methods, and
  // will fail the scope check.
  for (const auto& composed_protocol : protocol_declaration->composed_protocols) {
    if (!CompileAttributeList(composed_protocol.attributes.get())) {
      return false;
    }

    auto decl = LookupDeclByName(composed_protocol.name);
    if (!decl) {
      return Fail(ErrUnknownType, composed_protocol.name, composed_protocol.name);
    }
    if (decl->kind != Decl::Kind::kProtocol)
      return Fail(ErrComposingNonProtocol, composed_protocol.name);
    if (!CompileDecl(decl)) {
      return false;
    }
  }
  for (auto& method : protocol_declaration->methods) {
    if (!CompileAttributeList(method.attributes.get())) {
      return false;
    }

    auto selector = fidl::ordinals::GetSelector(method.attributes.get(), method.name);
    if (!utils::IsValidIdentifierComponent(selector) &&
        !utils::IsValidFullyQualifiedMethodIdentifier(selector)) {
      Fail(ErrInvalidSelectorValue, method.name);
    }
    method.generated_ordinal64 = std::make_unique<raw::Ordinal64>(method_hasher_(
        library_name_, protocol_declaration->name.decl_name(), selector, *method.identifier));
  }

  if (!CheckScopes(protocol_declaration, CheckScopes))
    return false;

  for (auto& method : protocol_declaration->methods) {
    if (method.maybe_request_payload) {
      if (!CompileDecl(method.maybe_request_payload))
        return false;
    }
    if (method.maybe_response_payload) {
      if (!CompileDecl(method.maybe_response_payload))
        return false;
    }
  }

  return true;
}

bool Library::CompileService(Service* service_decl) {
  Scope<std::string> scope;
  if (!CompileAttributeList(service_decl->attributes.get())) {
    return false;
  }

  for (auto& member : service_decl->members) {
    if (!CompileAttributeList(member.attributes.get())) {
      return false;
    }

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
    if (!CompileTypeConstructor(&member.type_ctor))
      return false;
    // There's a mismatch between the "default" allowed categories and what is actually allowed
    // in this context: in the new syntax, nothing changes. In the old syntax, we are more
    // restrictive in this context, requiring kProtocolOnly rather than kTypeOrProtocol (which is
    // the default for TypeConstructorOld).
    bool ok = std::visit(fidl::utils::matchers{
                             [this](const std::unique_ptr<TypeConstructorOld>& type_ctor) -> bool {
                               return VerifyTypeCategory(type_ctor->type, type_ctor->name.span(),
                                                         AllowedCategories::kProtocolOnly);
                             },
                             [&member, this](const std::unique_ptr<TypeConstructorNew>& t) -> bool {
                               if (t->type->kind != Type::Kind::kTransportSide)
                                 return Fail(ErrMustBeTransportSide, member.name);
                               return true;
                             }},
                         member.type_ctor);
    if (!ok)
      return false;
    if (GetType(member.type_ctor)->nullability != types::Nullability::kNonnullable)
      return Fail(ErrNullableServiceMember, member.name);
  }
  return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
  Scope<std::string> scope;
  DeriveResourceness derive_resourceness(&struct_declaration->resourceness);

  if (!CompileAttributeList(struct_declaration->attributes.get())) {
    return false;
  }

  for (auto& member : struct_declaration->members) {
    if (!CompileAttributeList(member.attributes.get())) {
      return false;
    }

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

    if (!CompileTypeConstructor(&member.type_ctor))
      return false;
    assert(!(struct_declaration->is_request_or_response && member.maybe_default_value) &&
           "method parameters cannot have default values");
    if (member.maybe_default_value) {
      const auto* default_value_type = GetType(member.type_ctor);
      if (!TypeCanBeConst(default_value_type)) {
        return Fail(ErrInvalidStructMemberType, *struct_declaration, NameIdentifier(member.name),
                    default_value_type);
      }
      if (!ResolveConstant(member.maybe_default_value.get(), default_value_type)) {
        return false;
      }
    }
    derive_resourceness.AddType(GetType(member.type_ctor));
  }

  return true;
}

bool Library::CompileTable(Table* table_declaration) {
  Scope<std::string> name_scope;
  Ordinal64Scope ordinal_scope;

  if (!CompileAttributeList(table_declaration->attributes.get())) {
    return false;
  }

  for (auto& member : table_declaration->members) {
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      return Fail(ErrDuplicateTableFieldOrdinal, member.ordinal->span(),
                  ordinal_result.previous_occurrence());
    }
    if (member.maybe_used) {
      if (!CompileAttributeList(member.maybe_used->attributes.get())) {
        return false;
      }

      auto& member_used = *member.maybe_used;
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
      if (!CompileTypeConstructor(&member_used.type_ctor)) {
        return false;
      }
      if (GetType(member_used.type_ctor)->nullability != types::Nullability::kNonnullable) {
        return Fail(ErrNullableTableMember, member_used.name);
      }
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    return Fail(ErrNonDenseOrdinal, span, ordinal);
  }

  return true;
}

bool Library::CompileUnion(Union* union_declaration) {
  Scope<std::string> scope;
  Ordinal64Scope ordinal_scope;
  DeriveResourceness derive_resourceness(&union_declaration->resourceness);

  if (!CompileAttributeList(union_declaration->attributes.get())) {
    return false;
  }

  for (const auto& member : union_declaration->members) {
    const auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->span());
    if (!ordinal_result.ok()) {
      return Fail(ErrDuplicateUnionMemberOrdinal, member.ordinal->span(),
                  ordinal_result.previous_occurrence());
    }
    if (member.maybe_used) {
      if (!CompileAttributeList(member.maybe_used->attributes.get())) {
        return false;
      }

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

      if (!CompileTypeConstructor(const_cast<TypeConstructor*>(&member_used.type_ctor))) {
        return false;
      }
      if (GetType(member_used.type_ctor)->nullability != types::Nullability::kNonnullable) {
        return Fail(ErrNullableUnionMember, member_used.name);
      }
      derive_resourceness.AddType(GetType(member_used.type_ctor));
    }
  }

  if (auto ordinal_and_loc = FindFirstNonDenseOrdinal(ordinal_scope)) {
    auto [ordinal, span] = *ordinal_and_loc;
    return Fail(ErrNonDenseOrdinal, span, ordinal);
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

bool Library::CompileTypeAlias(TypeAlias* type_alias) {
  if (!CompileAttributeList(type_alias->attributes.get())) {
    return false;
  }

  if (GetName(type_alias->partial_type_ctor) == type_alias->name)
    // fidlc's current semantics for cases like `alias foo = foo;` is to
    // include the LHS in the scope while compiling the RHS. Note that because
    // of an interaction with a fidlc scoping bug that prevents shadowing builtins,
    // this means that `alias Recursive = Recursive;` will fail with an includes
    // cycle error, but e.g. `alias uint32 = uint32;` won't because the user
    // defined `uint32` fails to shadow the builtin which means that we successfully
    // resolve the RHS. To avoid inconsistent semantics, we need to manually
    // catch this case and fail.
    return Fail(ErrIncludeCycle);
  return CompileTypeConstructor(&type_alias->partial_type_ctor);
}

bool Library::Compile() {
  if (!CompileAttributeList(attributes.get())) {
    return false;
  }

  // We process declarations in topologically sorted order. For
  // example, we process a struct member's type before the entire
  // struct.
  auto compile_step = StartCompileStep();
  for (auto& name_and_decl : declarations_) {
    Decl* decl = name_and_decl.second;
    compile_step.ForDecl(decl);
  }
  if (!compile_step.Done())
    return false;

  if (!SortDeclarations()) {
    return false;
  }

  auto verify_resourceness_step = StartVerifyResourcenessStep();
  for (const Decl* decl : declaration_order_) {
    verify_resourceness_step.ForDecl(decl);
  }
  if (!verify_resourceness_step.Done())
    return false;

  auto verify_attributes_step = StartVerifyAttributesStep();
  for (const Decl* decl : declaration_order_) {
    verify_attributes_step.ForDecl(decl);
  }
  if (!verify_attributes_step.Done())
    return false;

  for (const Decl* decl : declaration_order_) {
    if (decl->kind == Decl::Kind::kStruct) {
      auto struct_decl = static_cast<const Struct*>(decl);
      if (!VerifyInlineSize(struct_decl)) {
        return false;
      }
    }
  }

  if (!dependencies_.VerifyAllDependenciesWereUsed(*this, reporter_))
    return false;

  return reporter_->errors().size() == 0;
}

bool Library::CompileTypeConstructor(TypeConstructor* type_ctor) {
  return std::visit(fidl::utils::matchers{
                        [&, this](const std::unique_ptr<TypeConstructorOld>& type_ctor) -> bool {
                          return CompileTypeConstructorOld(type_ctor.get());
                        },
                        [&, this](const std::unique_ptr<TypeConstructorNew>& type_ctor) -> bool {
                          return CompileTypeConstructorNew(type_ctor.get());
                        },
                    },
                    *type_ctor);
}

bool Library::CompileTypeConstructorOld(TypeConstructorOld* type_ctor) {
  if (!typespace_->Create(LibraryMediator(this), type_ctor->name, type_ctor->maybe_arg_type_ctor,
                          type_ctor->handle_subtype_identifier, type_ctor->handle_rights,
                          type_ctor->maybe_size, type_ctor->nullability, &type_ctor->type,
                          &type_ctor->resolved_params))
    return false;

  // postcondition: compilation sets the Type of the TypeConstructor
  assert(type_ctor->type && "type constructors' type not resolved after compilation");
  return VerifyTypeCategory(type_ctor->type, type_ctor->name.span(),
                            AllowedCategories::kTypeOrProtocol);
}

bool Library::CompileTypeConstructorNew(TypeConstructorNew* type_ctor) {
  if (!typespace_->Create(LibraryMediator(this), type_ctor->name, type_ctor->parameters,
                          type_ctor->constraints, &type_ctor->type, &type_ctor->resolved_params))
    return false;

  // // postcondition: compilation sets the Type of the TypeConstructor
  assert(type_ctor->type && "type constructors' type not resolved after compilation");
  return VerifyTypeCategory(type_ctor->type, type_ctor->name.span(), AllowedCategories::kTypeOnly);
}

bool Library::VerifyTypeCategory(const Type* type, std::optional<SourceSpan> span,
                                 AllowedCategories category) {
  assert(type && "CompileTypeConstructor did not set Type");
  if (type->kind != Type::Kind::kIdentifier) {
    // we assume that all non-identifier types (i.e. builtins) are actually
    // types (and not e.g. protocols or services).
    return category == AllowedCategories::kProtocolOnly ? Fail(ErrCannotUseType, span) : true;
  }

  auto identifier_type = static_cast<const IdentifierType*>(type);
  switch (identifier_type->type_decl->kind) {
    // services are never allowed in any context
    case Decl::Kind::kService:
      return Fail(ErrCannotUseService, span);
      break;
    case Decl::Kind::kProtocol:
      if (category == AllowedCategories::kTypeOnly)
        return Fail(ErrCannotUseProtocol, span);
      break;
    default:
      if (category == AllowedCategories::kProtocolOnly)
        return Fail(ErrCannotUseType, span);
      break;
  }
  return true;
}

bool Library::ResolveHandleRightsConstant(Resource* resource, Constant* constant,
                                          const HandleRights** out_rights) {
  if (!IsTypeConstructorDefined(resource->subtype_ctor) ||
      GetName(resource->subtype_ctor).full_name() != "uint32") {
    return Fail(ErrResourceMustBeUint32Derived, resource->name);
  }

  auto rights_property = resource->LookupProperty("rights");
  if (!rights_property) {
    return false;
  }

  Decl* rights_decl = LookupDeclByName(GetName(rights_property->type_ctor));
  if (!rights_decl || rights_decl->kind != Decl::Kind::kBits) {
    return false;
  }

  if (!GetType(rights_property->type_ctor)) {
    if (!CompileTypeConstructor(&rights_property->type_ctor))
      return false;
  }
  const Type* rights_type = GetType(rights_property->type_ctor);

  if (!ResolveConstant(constant, rights_type))
    return false;

  if (out_rights)
    *out_rights = static_cast<const HandleRights*>(&constant->Value());
  return true;
}

bool Library::ResolveHandleSubtypeIdentifier(Resource* resource,
                                             const std::unique_ptr<Constant>& constant,
                                             uint32_t* out_obj_type) {
  // We only support an extremely limited form of resource suitable for
  // handles here, where it must be:
  // - derived from uint32
  // - have a single properties element
  // - the single property element must be a reference to an enum
  // - the single property must be named "subtype".
  if (constant->kind != Constant::Kind::kIdentifier) {
    return false;
  }
  auto identifier_constant = static_cast<IdentifierConstant*>(constant.get());
  const Name& handle_subtype_identifier = identifier_constant->name;

  if (!IsTypeConstructorDefined(resource->subtype_ctor) ||
      GetName(resource->subtype_ctor).full_name() != "uint32") {
    return false;
  }
  auto subtype_property = resource->LookupProperty("subtype");
  if (!subtype_property) {
    return false;
  }

  Decl* subtype_decl = LookupDeclByName(GetName(subtype_property->type_ctor));
  if (!subtype_decl || subtype_decl->kind != Decl::Kind::kEnum) {
    return false;
  }

  if (!GetType(subtype_property->type_ctor)) {
    if (!CompileTypeConstructor(&subtype_property->type_ctor))
      return false;
  }
  const Type* subtype_type = GetType(subtype_property->type_ctor);

  auto* subtype_enum = static_cast<Enum*>(subtype_decl);
  for (const auto& member : subtype_enum->members) {
    if (member.name.data() == handle_subtype_identifier.span()->data()) {
      if (!ResolveConstant(member.value.get(), subtype_type)) {
        return false;
      }
      const flat::ConstantValue& value = member.value->Value();
      auto obj_type = static_cast<uint32_t>(
          reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value));
      *out_obj_type = obj_type;
      return true;
    }
  }

  return false;
}

bool Library::ResolveSizeBound(Constant* size_constant, const Size** out_size) {
  if (!ResolveConstant(size_constant, &kSizeType)) {
    if (size_constant->kind == Constant::Kind::kIdentifier) {
      auto name = static_cast<IdentifierConstant*>(size_constant)->name;
      if (name.library() == this && name.decl_name() == "MAX" && !name.member_name()) {
        size_constant->ResolveTo(std::make_unique<Size>(Size::Max()));
      }
    }
  }
  if (!size_constant->IsResolved()) {
    return false;
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

    if (!ResolveConstant(member.value.get(), GetType(decl->subtype_ctor))) {
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
  auto validator = [&mask](MemberType member, const AttributeList*) -> std::unique_ptr<Diagnostic> {
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
    if (!ResolveConstant(member.value.get(), GetType(enum_decl->subtype_ctor))) {
      return Fail(ErrCouldNotResolveMember, member.name, std::string("enum"));
    }
    auto attributes = member.attributes.get();
    if (attributes && attributes->HasAttribute("unknown")) {
      unknown_value =
          static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
    }
  }
  *out_unknown_value = unknown_value;

  auto validator = [enum_decl, unknown_value](
                       MemberType member,
                       const AttributeList* attributes) -> std::unique_ptr<Diagnostic> {
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

    if (attributes && attributes->HasAttribute("unknown"))
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
  if (!attributes)
    return false;
  return attributes->HasAttribute(std::string(name));
}

const std::set<Library*>& Library::dependencies() const { return dependencies_.dependencies(); }

std::set<const Library*, LibraryComparator> Library::DirectDependencies() const {
  std::set<const Library*, LibraryComparator> direct_dependencies;
  auto add_constant_deps = [&](const Constant* constant) {
    if (constant->kind != Constant::Kind::kIdentifier)
      return;
    auto* dep_library = static_cast<const IdentifierConstant*>(constant)->name.library();
    assert(dep_library != nullptr && "all identifier constants have a library");
    direct_dependencies.insert(dep_library);
  };
  auto add_type_ctor_deps = [&](const TypeConstructor& type_ctor) {
    if (auto dep_library = GetName(type_ctor).library())
      direct_dependencies.insert(dep_library);

    // TODO(fxbug.dev/64629): Add dependencies introduced through handle constraints.
    // This code currently assumes the handle constraints are always defined in the same
    // library as the resource_definition and so does not check for them separately.
    const auto& invocation = GetLayoutInvocation(type_ctor);
    if (invocation.size_raw)
      add_constant_deps(invocation.size_raw);
    if (invocation.protocol_decl_raw)
      add_constant_deps(invocation.protocol_decl_raw);
    if (IsTypeConstructorDefined(invocation.element_type_raw)) {
      if (auto dep_library = GetName(invocation.element_type_raw).library())
        direct_dependencies.insert(dep_library);
    }
    if (IsTypeConstructorDefined(invocation.boxed_type_raw)) {
      if (auto dep_library = GetName(invocation.boxed_type_raw).library())
        direct_dependencies.insert(dep_library);
    }
  };
  for (const auto& dep_library : dependencies()) {
    direct_dependencies.insert(dep_library);
  }
  // Discover additional dependencies that are required to support
  // cross-library protocol composition.
  for (const auto& protocol : protocol_declarations_) {
    for (const auto method_with_info : protocol->all_methods) {
      if (auto request = method_with_info.method->maybe_request_payload) {
        for (const auto& member : request->members) {
          add_type_ctor_deps(member.type_ctor);
        }
      }
      if (auto response = method_with_info.method->maybe_response_payload) {
        for (const auto& member : response->members) {
          add_type_ctor_deps(member.type_ctor);
        }
      }
      direct_dependencies.insert(method_with_info.method->owning_protocol->name.library());
    }
  }
  direct_dependencies.erase(this);
  return direct_dependencies;
}

std::unique_ptr<TypeConstructorOld> TypeConstructorOld::CreateSizeType() {
  return std::make_unique<TypeConstructorOld>(
      Name::CreateIntrinsic("uint32"), nullptr /* maybe_arg_type */,
      std::optional<Name>() /* handle_subtype_identifier */, nullptr /* handle_rights */,
      nullptr /* maybe_size */, types::Nullability::kNonnullable);
}

std::unique_ptr<TypeConstructorNew> TypeConstructorNew::CreateSizeType() {
  std::vector<std::unique_ptr<LayoutParameter>> no_params;
  std::vector<std::unique_ptr<Constant>> no_constraints;
  return std::make_unique<TypeConstructorNew>(
      Name::CreateIntrinsic("uint32"),
      std::make_unique<LayoutParameterList>(std::move(no_params), std::nullopt /* span */),
      std::make_unique<TypeConstraints>(std::move(no_constraints), std::nullopt /* span */));
}

bool LibraryMediator::ResolveParamAsType(const flat::TypeTemplate* layout,
                                         const std::unique_ptr<LayoutParameter>& param,
                                         const Type** out_type) const {
  auto type_ctor = param->AsTypeCtor();
  auto check = library_->reporter_->Checkpoint();
  if (!type_ctor || !ResolveType(type_ctor)) {
    // if there were no errors reported but we couldn't resolve to a type, it must
    // mean that the parameter referred to a non-type, so report a new error here.
    if (check.NoNewErrors()) {
      return library_->Fail(ErrExpectedType, param->span);
    }
    // otherwise, there was an error during the type resolution process, so we
    // should just report that rather than add an extra error here
    return false;
  }
  *out_type = type_ctor->type;
  return true;
}

bool LibraryMediator::ResolveParamAsSize(const flat::TypeTemplate* layout,
                                         const std::unique_ptr<LayoutParameter>& param,
                                         const Size** out_size) const {
  // We could use param->AsConstant() here, leading to code similar to ResolveParamAsType.
  // However, unlike ErrExpectedType, ErrExpectedValueButGotType requires a name to be
  // reported, which would require doing a switch on the parameter kind anyway to find
  // its Name. So we just handle all the cases ourselves from the start.
  switch (param->kind) {
    case LayoutParameter::Kind::kLiteral: {
      auto literal_param = static_cast<LiteralLayoutParameter*>(param.get());
      if (!ResolveSizeBound(literal_param->literal.get(), out_size))
        return library_->Fail(ErrCouldNotParseSizeBound);
      break;
    }
    case LayoutParameter::kType: {
      auto type_param = static_cast<TypeLayoutParameter*>(param.get());
      return library_->Fail(ErrExpectedValueButGotType, type_param->type_ctor->name);
    }
    case LayoutParameter::Kind::kIdentifier: {
      auto ambig_param = static_cast<IdentifierLayoutParameter*>(param.get());
      auto as_constant = ambig_param->AsConstant();
      if (!ResolveSizeBound(as_constant, out_size))
        return library_->Fail(ErrExpectedValueButGotType, ambig_param->name);
      break;
    }
  }
  assert(*out_size);
  if ((*out_size)->value == 0)
    return library_->Fail(ErrMustHaveNonZeroSize, param->span, layout);
  return true;
}

bool LibraryMediator::ResolveConstraintAs(const std::unique_ptr<Constant>& constraint,
                                          const std::vector<ConstraintKind>& interpretations,
                                          Resource* resource, ResolvedConstraint* out) const {
  for (const auto& constraint_kind : interpretations) {
    out->kind = constraint_kind;
    switch (constraint_kind) {
      case ConstraintKind::kHandleSubtype: {
        assert(resource &&
               "Compiler bug: must pass resource if trying to resolve to handle subtype");
        if (ResolveAsHandleSubtype(resource, constraint, &out->value.handle_subtype))
          return true;
        break;
      }
      case ConstraintKind::kHandleRights: {
        assert(resource &&
               "Compiler bug: must pass resource if trying to resolve to handle rights");
        if (ResolveAsHandleRights(resource, constraint.get(), &(out->value.handle_rights)))
          return true;
        break;
      }
      case ConstraintKind::kSize: {
        if (ResolveSizeBound(constraint.get(), &(out->value.size)))
          return true;
        break;
      }
      case ConstraintKind::kNullability: {
        if (ResolveAsOptional(constraint.get()))
          return true;
        break;
      }
      case ConstraintKind::kProtocol: {
        if (ResolveAsProtocol(constraint.get(), &(out->value.protocol_decl)))
          return true;
        break;
      }
    }
  }
  return false;
}

bool LibraryMediator::ResolveType(TypeConstructorOld* type) const {
  return library_->CompileTypeConstructorOld(type);
}

bool LibraryMediator::ResolveType(TypeConstructorNew* type) const {
  return library_->CompileTypeConstructorNew(type);
}

bool LibraryMediator::ResolveSizeBound(Constant* size_constant, const Size** out_size) const {
  return library_->ResolveSizeBound(size_constant, out_size);
}

bool LibraryMediator::ResolveAsOptional(Constant* constant) const {
  return library_->ResolveAsOptional(constant);
}

bool LibraryMediator::ResolveAsHandleSubtype(Resource* resource,
                                             const std::unique_ptr<Constant>& constant,
                                             uint32_t* out_obj_type) const {
  return library_->ResolveHandleSubtypeIdentifier(resource, constant, out_obj_type);
}

bool LibraryMediator::ResolveAsHandleRights(Resource* resource, Constant* constant,
                                            const HandleRights** out_rights) const {
  return library_->ResolveHandleRightsConstant(resource, constant, out_rights);
}

bool LibraryMediator::ResolveAsProtocol(const Constant* constant, const Protocol** out_decl) const {
  // TODO(fxbug.dev/75112): If/when this method is responsible for reporting errors, the
  // `return false` statements should fail with ErrConstraintMustBeProtocol instead.
  if (constant->kind != Constant::Kind::kIdentifier)
    return false;

  const auto* as_identifier = static_cast<const IdentifierConstant*>(constant);
  const auto* decl = LookupDeclByName(as_identifier->name);
  if (!decl || decl->kind != Decl::Kind::kProtocol)
    return false;
  *out_decl = static_cast<const Protocol*>(decl);
  return true;
}

template <typename... Args>
bool LibraryMediator::Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                           const Args&... args) const {
  return library_->Fail(err, span, args...);
}

Decl* LibraryMediator::LookupDeclByName(Name::Key name) const {
  return library_->LookupDeclByName(name);
}

TypeConstructorNew* LiteralLayoutParameter::AsTypeCtor() const { return nullptr; }
TypeConstructorNew* TypeLayoutParameter::AsTypeCtor() const { return type_ctor.get(); }
TypeConstructorNew* IdentifierLayoutParameter::AsTypeCtor() const {
  if (!as_type_ctor) {
    std::vector<std::unique_ptr<LayoutParameter>> no_params;
    std::vector<std::unique_ptr<Constant>> no_constraints;
    as_type_ctor = std::make_unique<TypeConstructorNew>(
        name, std::make_unique<LayoutParameterList>(std::move(no_params), std::nullopt),
        std::make_unique<TypeConstraints>(std::move(no_constraints), std::nullopt));
  }

  return as_type_ctor.get();
}

Constant* LiteralLayoutParameter::AsConstant() const { return literal.get(); }
Constant* TypeLayoutParameter::AsConstant() const { return nullptr; }
Constant* IdentifierLayoutParameter::AsConstant() const {
  if (!as_constant) {
    as_constant = std::make_unique<IdentifierConstant>(name, span);
  }
  return as_constant.get();
}

bool LibraryMediator::CompileDecl(Decl* decl) const { return library_->CompileDecl(decl); }

}  // namespace flat
}  // namespace fidl
