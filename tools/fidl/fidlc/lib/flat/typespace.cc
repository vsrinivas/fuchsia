// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/typespace.h"

#include "fidl/flat/type_resolver.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

constexpr std::string_view kChannelTransport = "Channel";

bool Typespace::Create(TypeResolver* resolver, const flat::Name& name,
                       const std::unique_ptr<LayoutParameterList>& parameters,
                       const std::unique_ptr<TypeConstraints>& constraints, const Type** out_type,
                       LayoutInvocation* out_params,
                       const std::optional<SourceSpan>& type_ctor_span) {
  std::unique_ptr<Type> type;
  if (!CreateNotOwned(resolver, name, parameters, constraints, &type, out_params, type_ctor_span))
    return false;
  types_.push_back(std::move(type));
  *out_type = types_.back().get();
  return true;
}

bool Typespace::CreateNotOwned(TypeResolver* resolver, const flat::Name& name,
                               const std::unique_ptr<LayoutParameterList>& parameters,
                               const std::unique_ptr<TypeConstraints>& constraints,
                               std::unique_ptr<Type>* out_type, LayoutInvocation* out_params,
                               const std::optional<SourceSpan>& type_ctor_span) {
  // TODO(pascallouis): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name,
  // arg_type, size, and nullability.

  auto type_template = LookupTemplate(name);
  if (type_template == nullptr) {
    return Fail(ErrUnknownType, name.span().value(), name);
  }
  if (type_template->HasGeneratedName() && (name.as_anonymous() == nullptr)) {
    return Fail(ErrAnonymousNameReference, name.span().value(), name);
  }
  return type_template->Create(
      resolver,
      {.parameters = parameters, .constraints = constraints, .type_ctor_span = type_ctor_span},
      out_type, out_params);
}

const SourceSpan& TypeTemplate::ParamsAndConstraints::ParametersSpan() const {
  auto num_params = parameters->items.size();
  if (num_params > 0)
    return parameters->span.value();
  if (type_ctor_span)
    return *type_ctor_span;
  // Fallback empty span
  return parameters->span.value();
}

bool TypeTemplate::EnsureNumberOfLayoutParams(const ParamsAndConstraints& unresolved_args,
                                              size_t expected_params) const {
  auto num_params = unresolved_args.parameters->items.size();
  if (num_params != expected_params) {
    Fail(ErrWrongNumberOfLayoutParameters, unresolved_args.ParametersSpan(), this, expected_params,
         num_params);
    return false;
  }
  return true;
}

const Size* Typespace::InternSize(uint32_t size) {
  sizes_.push_back(std::make_unique<Size>(size));
  return sizes_.back().get();
}

const Type* Typespace::Intern(std::unique_ptr<Type> type) {
  types_.push_back(std::move(type));
  return types_.back().get();
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

bool TypeTemplate::HasGeneratedName() const { return name_.as_anonymous() != nullptr; }

Typespace Typespace::RootTypes(Reporter* reporter) {
  Typespace root_typespace(reporter);

  auto add_template = [&](std::unique_ptr<TypeTemplate> type_template) {
    const Name& name = type_template->name();
    root_typespace.templates_.emplace(name, std::move(type_template));
  };

  auto add_primitive = [&](std::string name, types::PrimitiveSubtype subtype) {
    add_template(std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, std::move(name),
                                                         subtype));
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
  root_typespace.templates_.emplace(
      kByteName, std::make_unique<PrimitiveTypeTemplate>(&root_typespace, reporter, "uint8",
                                                         types::PrimitiveSubtype::kUint8));
  root_typespace.templates_.emplace(kBytesName,
                                    std::make_unique<BytesTypeTemplate>(&root_typespace, reporter));

  add_template(std::make_unique<ArrayTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<VectorTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<StringTypeTemplate>(&root_typespace, reporter));
  add_template(std::make_unique<TransportSideTypeTemplate>(
      &root_typespace, reporter, TransportSide::kServer, kChannelTransport));
  add_template(std::make_unique<TransportSideTypeTemplate>(
      &root_typespace, reporter, TransportSide::kClient, kChannelTransport));
  add_template(std::make_unique<BoxTypeTemplate>(&root_typespace, reporter));
  return root_typespace;
}

// static
const Name Typespace::kBoolTypeName = Name::CreateIntrinsic("bool");
const Name Typespace::kInt8TypeName = Name::CreateIntrinsic("int8");
const Name Typespace::kInt16TypeName = Name::CreateIntrinsic("int16");
const Name Typespace::kInt32TypeName = Name::CreateIntrinsic("int32");
const Name Typespace::kInt64TypeName = Name::CreateIntrinsic("int64");
const Name Typespace::kUint8TypeName = Name::CreateIntrinsic("uint8");
const Name Typespace::kUint16TypeName = Name::CreateIntrinsic("uint16");
const Name Typespace::kUint32TypeName = Name::CreateIntrinsic("uint32");
const Name Typespace::kUint64TypeName = Name::CreateIntrinsic("uint64");
const Name Typespace::kFloat32TypeName = Name::CreateIntrinsic("float32");
const Name Typespace::kFloat64TypeName = Name::CreateIntrinsic("float64");
const Name Typespace::kUntypedNumericTypeName = Name::CreateIntrinsic("untyped numeric");
const Name Typespace::kStringTypeName = Name::CreateIntrinsic("string");
const PrimitiveType Typespace::kBoolType =
    PrimitiveType(kBoolTypeName, types::PrimitiveSubtype::kBool);
const PrimitiveType Typespace::kInt8Type =
    PrimitiveType(kInt8TypeName, types::PrimitiveSubtype::kInt8);
const PrimitiveType Typespace::kInt16Type =
    PrimitiveType(kInt16TypeName, types::PrimitiveSubtype::kInt16);
const PrimitiveType Typespace::kInt32Type =
    PrimitiveType(kInt32TypeName, types::PrimitiveSubtype::kInt32);
const PrimitiveType Typespace::kInt64Type =
    PrimitiveType(kInt64TypeName, types::PrimitiveSubtype::kInt64);
const PrimitiveType Typespace::kUint8Type =
    PrimitiveType(kUint8TypeName, types::PrimitiveSubtype::kUint8);
const PrimitiveType Typespace::kUint16Type =
    PrimitiveType(kUint16TypeName, types::PrimitiveSubtype::kUint16);
const PrimitiveType Typespace::kUint32Type =
    PrimitiveType(kUint32TypeName, types::PrimitiveSubtype::kUint32);
const PrimitiveType Typespace::kUint64Type =
    PrimitiveType(kUint64TypeName, types::PrimitiveSubtype::kUint64);
const PrimitiveType Typespace::kFloat32Type =
    PrimitiveType(kFloat32TypeName, types::PrimitiveSubtype::kFloat32);
const PrimitiveType Typespace::kFloat64Type =
    PrimitiveType(kFloat64TypeName, types::PrimitiveSubtype::kFloat64);
const UntypedNumericType Typespace::kUntypedNumericType =
    UntypedNumericType(kUntypedNumericTypeName);
const StringType Typespace::kUnboundedStringType = StringType(
    Typespace::kStringTypeName, &VectorBaseType::kMaxSize, types::Nullability::kNonnullable);

bool ArrayTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                               std::unique_ptr<Type>* out_type,
                               LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 2))
    return false;

  const Type* element_type = nullptr;
  if (!resolver->ResolveParamAsType(this, unresolved_args.parameters->items[0], &element_type))
    return false;
  out_params->element_type_resolved = element_type;
  out_params->element_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

  const Size* size = nullptr;
  if (!resolver->ResolveParamAsSize(this, unresolved_args.parameters->items[1], &size))
    return false;
  out_params->size_resolved = size;
  out_params->size_raw = unresolved_args.parameters->items[1]->AsConstant();

  ArrayType type(name_, element_type, size);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool BytesTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                               std::unique_ptr<Type>* out_type,
                               LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  VectorType type(name_, &uint8_type_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool VectorTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                                std::unique_ptr<Type>* out_type,
                                LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 1))
    return false;

  const Type* element_type = nullptr;
  if (!resolver->ResolveParamAsType(this, unresolved_args.parameters->items[0], &element_type))
    return false;
  out_params->element_type_resolved = element_type;
  out_params->element_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

  VectorType type(name_, element_type);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool StringTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                                std::unique_ptr<Type>* out_type,
                                LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  StringType type(name_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

const HandleRights HandleType::kSameRights = HandleRights(kHandleSameRights);

bool HandleTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                                std::unique_ptr<Type>* out_type,
                                LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  HandleType type(name_, resource_decl_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool TransportSideTypeTemplate::Create(TypeResolver* resolver,
                                       const ParamsAndConstraints& unresolved_args,
                                       std::unique_ptr<Type>* out_type,
                                       LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  TransportSideType type(name_, end_, protocol_transport_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool TypeDeclTypeTemplate::Create(TypeResolver* resolver,
                                  const ParamsAndConstraints& unresolved_args,
                                  std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  if (!type_decl_->compiled && type_decl_->kind != Decl::Kind::kProtocol) {
    if (type_decl_->compiling) {
      type_decl_->recursive = true;
    } else {
      resolver->CompileDecl(type_decl_);
    }
  }

  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  IdentifierType type(name_, type_decl_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool TypeAliasTypeTemplate::Create(TypeResolver* resolver,
                                   const ParamsAndConstraints& unresolved_args,
                                   std::unique_ptr<Type>* out_type,
                                   LayoutInvocation* out_params) const {
  if (auto cycle = resolver->GetDeclCycle(decl_); cycle) {
    return Fail(ErrIncludeCycle, decl_->name.span().value(), cycle.value());
  }
  resolver->CompileDecl(decl_);

  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  // Compilation failed while trying to resolve something farther up the chain;
  // exit early
  if (decl_->partial_type_ctor->type == nullptr)
    return false;
  const auto& aliased_type = decl_->partial_type_ctor->type;
  out_params->from_type_alias = decl_;
  return aliased_type->ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type,
                                        out_params);
}

static bool IsStruct(const Type* boxed_type) {
  if (!boxed_type || boxed_type->kind != Type::Kind::kIdentifier)
    return false;

  return static_cast<const IdentifierType*>(boxed_type)->type_decl->kind == Decl::Kind::kStruct;
}

bool BoxTypeTemplate::Create(TypeResolver* resolver, const ParamsAndConstraints& unresolved_args,
                             std::unique_ptr<Type>* out_type, LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 1))
    return false;

  const Type* boxed_type = nullptr;
  if (!resolver->ResolveParamAsType(this, unresolved_args.parameters->items[0], &boxed_type))
    return false;
  if (!IsStruct(boxed_type))
    return Fail(ErrCannotBeBoxed, unresolved_args.parameters->items[0]->span, boxed_type->name);
  const auto* inner = static_cast<const IdentifierType*>(boxed_type);
  if (inner->nullability == types::Nullability::kNullable) {
    return Fail(ErrBoxedTypeCannotBeNullable, unresolved_args.parameters->items[0]->span);
  }
  // We disallow specifying the boxed type as nullable in FIDL source but
  // then mark the boxed type as nullable, so that internally it shares the
  // same code path as its old syntax equivalent (a nullable struct). This
  // allows us to call `f(type->boxed_type)` wherever we used to call `f(type)`
  // in the old code.
  // As a temporary workaround for piping unconst-ness everywhere or having
  // box types own their own boxed types, we cast away the const to be able
  // to change the boxed type to be mutable.
  auto* mutable_inner = const_cast<IdentifierType*>(inner);
  mutable_inner->nullability = types::Nullability::kNullable;

  out_params->boxed_type_resolved = boxed_type;
  out_params->boxed_type_raw = unresolved_args.parameters->items[0]->AsTypeCtor();

  BoxType type(name_, boxed_type);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

bool PrimitiveTypeTemplate::Create(TypeResolver* resolver,
                                   const ParamsAndConstraints& unresolved_args,
                                   std::unique_ptr<Type>* out_type,
                                   LayoutInvocation* out_params) const {
  if (!EnsureNumberOfLayoutParams(unresolved_args, 0))
    return false;

  // TODO(fxbug.dev/76219): Should instead use the static const types provided
  // on Typespace, e.g. Typespace::kBoolType.
  PrimitiveType type(name_, subtype_);
  return type.ApplyConstraints(resolver, *unresolved_args.constraints, this, out_type, out_params);
}

}  // namespace fidl::flat
