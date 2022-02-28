// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/typespace.h"

#include "fidl/flat/type_resolver.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

static const Size kMaxSize = Size::Max();

static std::optional<types::PrimitiveSubtype> BuiltinKindToPrimitiveSubtype(Builtin::Kind kind) {
  switch (kind) {
    case Builtin::Kind::kBool:
      return types::PrimitiveSubtype::kBool;
    case Builtin::Kind::kInt8:
      return types::PrimitiveSubtype::kInt8;
    case Builtin::Kind::kInt16:
      return types::PrimitiveSubtype::kInt16;
    case Builtin::Kind::kInt32:
      return types::PrimitiveSubtype::kInt32;
    case Builtin::Kind::kInt64:
      return types::PrimitiveSubtype::kInt64;
    case Builtin::Kind::kUint8:
      return types::PrimitiveSubtype::kUint8;
    case Builtin::Kind::kUint16:
      return types::PrimitiveSubtype::kUint16;
    case Builtin::Kind::kUint32:
      return types::PrimitiveSubtype::kUint32;
    case Builtin::Kind::kUint64:
      return types::PrimitiveSubtype::kUint64;
    case Builtin::Kind::kFloat32:
      return types::PrimitiveSubtype::kFloat32;
    case Builtin::Kind::kFloat64:
      return types::PrimitiveSubtype::kFloat64;
    default:
      return std::nullopt;
  }
}

Typespace::Typespace(const Library* root_library, Reporter* reporter) : ReporterMixin(reporter) {
  for (auto& builtin : root_library->builtin_declarations) {
    if (auto subtype = BuiltinKindToPrimitiveSubtype(builtin->kind)) {
      primitive_types_.emplace(subtype.value(),
                               std::make_unique<PrimitiveType>(builtin->name, subtype.value()));
    } else if (builtin->kind == Builtin::Kind::kString) {
      unbounded_string_type_ =
          std::make_unique<StringType>(builtin->name, &kMaxSize, types::Nullability::kNonnullable);
    } else if (builtin->kind == Builtin::Kind::kVector) {
      vector_layout_name_ = builtin->name;
    }
  }
  untyped_numeric_type_ =
      std::make_unique<UntypedNumericType>(Name::CreateIntrinsic(nullptr, "untyped numeric"));
}

const PrimitiveType* Typespace::GetPrimitiveType(types::PrimitiveSubtype subtype) {
  return primitive_types_.at(subtype).get();
}

const Type* Typespace::GetUnboundedStringType() { return unbounded_string_type_.get(); }

const Type* Typespace::GetStringType(size_t max_size) {
  auto name = unbounded_string_type_->name;
  sizes_.push_back(std::make_unique<Size>(max_size));
  auto size = sizes_.back().get();
  types_.push_back(std::make_unique<StringType>(name, size, types::Nullability::kNonnullable));
  return types_.back().get();
}

const Type* Typespace::GetUntypedNumericType() { return untyped_numeric_type_.get(); }

const Type* Typespace::Intern(std::unique_ptr<Type> type) {
  types_.push_back(std::move(type));
  return types_.back().get();
}

class Typespace::Creator : private ReporterMixin {
 public:
  Creator(Typespace* typespace, TypeResolver* resolver, const Reference& layout,
          const LayoutParameterList& parameters, const TypeConstraints& constraints,
          LayoutInvocation* out_params)
      : ReporterMixin(typespace->reporter()),
        typespace_(typespace),
        resolver_(resolver),
        layout_(layout),
        parameters_(parameters),
        constraints_(constraints),
        out_params_(out_params) {}

  const Type* Create();

 private:
  bool EnsureNumberOfLayoutParams(size_t expected_params);

  const Type* CreatePrimitiveType(types::PrimitiveSubtype subtype);
  const Type* CreateStringType();
  const Type* CreateArrayType();
  const Type* CreateVectorType();
  const Type* CreateBytesType();
  const Type* CreateBoxType();
  const Type* CreateHandleType(Resource* resource);
  const Type* CreateTransportSideType(TransportSide end);
  const Type* CreateIdentifierType(TypeDecl* type_decl);
  const Type* CreateTypeAliasType(TypeAlias* type_alias);

  Typespace* typespace_;
  TypeResolver* resolver_;
  const Reference& layout_;
  const LayoutParameterList& parameters_;
  const TypeConstraints& constraints_;
  LayoutInvocation* out_params_;
};

const Type* Typespace::Create(TypeResolver* resolver, const Reference& layout,
                              const LayoutParameterList& parameters,
                              const TypeConstraints& constraints, LayoutInvocation* out_params) {
  // TODO(fxbug.dev/76219): lookup whether we've already created the type, and
  // return it rather than create a new one. Lookup must be by name, arg_type,
  // size, and nullability.
  return Creator(this, resolver, layout, parameters, constraints, out_params).Create();
}

const Type* Typespace::Creator::Create() {
  Decl* target = layout_.target()->AsDecl();

  switch (target->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kStruct:
    case Decl::Kind::kTable:
    case Decl::Kind::kUnion:
      return CreateIdentifierType(static_cast<TypeDecl*>(target));
    case Decl::Kind::kResource:
      return CreateHandleType(static_cast<Resource*>(target));
    case Decl::Kind::kTypeAlias:
      return CreateTypeAliasType(static_cast<TypeAlias*>(target));
    case Decl::Kind::kBuiltin:
      // Handled below.
      break;
    default:
      Fail(ErrExpectedType, layout_.span().value());
      return nullptr;
  }

  auto builtin = static_cast<const Builtin*>(target);
  switch (builtin->kind) {
    case Builtin::Kind::kBool:
    case Builtin::Kind::kInt8:
    case Builtin::Kind::kInt16:
    case Builtin::Kind::kInt32:
    case Builtin::Kind::kInt64:
    case Builtin::Kind::kUint8:
    case Builtin::Kind::kUint16:
    case Builtin::Kind::kUint32:
    case Builtin::Kind::kUint64:
    case Builtin::Kind::kFloat32:
    case Builtin::Kind::kFloat64:
      return CreatePrimitiveType(BuiltinKindToPrimitiveSubtype(builtin->kind).value());
    case Builtin::Kind::kString:
      return CreateStringType();
    case Builtin::Kind::kBox:
      return CreateBoxType();
    case Builtin::Kind::kArray:
      return CreateArrayType();
    case Builtin::Kind::kVector:
      return CreateVectorType();
    case Builtin::Kind::kClientEnd:
      return CreateTransportSideType(TransportSide::kClient);
    case Builtin::Kind::kServerEnd:
      return CreateTransportSideType(TransportSide::kServer);
    case Builtin::Kind::kByte:
      return CreatePrimitiveType(types::PrimitiveSubtype::kUint8);
    case Builtin::Kind::kBytes:
      return CreateBytesType();
    case Builtin::Kind::kOptional:
    case Builtin::Kind::kMax:
      Fail(ErrExpectedType, layout_.span().value());
      return nullptr;
  }
}

bool Typespace::Creator::EnsureNumberOfLayoutParams(size_t expected_params) {
  auto num_params = parameters_.items.size();
  if (num_params == expected_params) {
    return true;
  }
  auto span = num_params == 0 ? layout_.span().value() : parameters_.span.value();
  return Fail(ErrWrongNumberOfLayoutParameters, span, layout_.target_name(), expected_params,
              num_params);
}

const Type* Typespace::Creator::CreatePrimitiveType(types::PrimitiveSubtype subtype) {
  if (!EnsureNumberOfLayoutParams(0)) {
    return nullptr;
  }
  std::unique_ptr<Type> constrained_type;
  typespace_->GetPrimitiveType(subtype)->ApplyConstraints(resolver_, constraints_, layout_,
                                                          &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateArrayType() {
  if (!EnsureNumberOfLayoutParams(2))
    return nullptr;

  const Type* element_type = nullptr;
  if (!resolver_->ResolveParamAsType(layout_, parameters_.items[0], &element_type))
    return nullptr;
  out_params_->element_type_resolved = element_type;
  out_params_->element_type_raw = parameters_.items[0]->AsTypeCtor();

  const Size* size = nullptr;
  if (!resolver_->ResolveParamAsSize(layout_, parameters_.items[1], &size))
    return nullptr;
  out_params_->size_resolved = size;
  out_params_->size_raw = parameters_.items[1]->AsConstant();

  ArrayType type(layout_.target_name(), element_type, size);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateVectorType() {
  if (!EnsureNumberOfLayoutParams(1))
    return nullptr;

  const Type* element_type = nullptr;
  if (!resolver_->ResolveParamAsType(layout_, parameters_.items[0], &element_type))
    return nullptr;
  out_params_->element_type_resolved = element_type;
  out_params_->element_type_raw = parameters_.items[0]->AsTypeCtor();

  VectorType type(layout_.target_name(), element_type);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateBytesType() {
  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  // Note that we name the type "vector", not "bytes".
  VectorType type(typespace_->vector_layout_name_.value(),
                  typespace_->GetPrimitiveType(types::PrimitiveSubtype::kUint8));
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateStringType() {
  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  StringType type(layout_.target_name());
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateHandleType(Resource* resource) {
  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  HandleType type(layout_.target_name(), resource);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

// TODO(fxbug.dev/56727): Support more transports.
static constexpr std::string_view kChannelTransport = "Channel";

const Type* Typespace::Creator::CreateTransportSideType(TransportSide end) {
  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  TransportSideType type(layout_.target_name(), end, kChannelTransport);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateIdentifierType(TypeDecl* type_decl) {
  if (!type_decl->compiled && type_decl->kind != Decl::Kind::kProtocol) {
    if (type_decl->compiling) {
      type_decl->recursive = true;
    } else {
      resolver_->CompileDecl(type_decl);
    }
  }

  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  IdentifierType type(type_decl);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

const Type* Typespace::Creator::CreateTypeAliasType(TypeAlias* type_alias) {
  if (auto cycle = resolver_->GetDeclCycle(type_alias); cycle) {
    Fail(ErrIncludeCycle, type_alias->name.span().value(), cycle.value());
    return nullptr;
  }
  resolver_->CompileDecl(type_alias);

  if (!EnsureNumberOfLayoutParams(0))
    return nullptr;

  // Compilation failed while trying to resolve something farther up the chain;
  // exit early
  if (type_alias->partial_type_ctor->type == nullptr)
    return nullptr;
  const auto& aliased_type = type_alias->partial_type_ctor->type;
  out_params_->from_type_alias = type_alias;
  std::unique_ptr<Type> constrained_type;
  aliased_type->ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

static bool IsStruct(const Type* type) {
  if (type->kind != Type::Kind::kIdentifier) {
    return false;
  }
  return static_cast<const IdentifierType*>(type)->type_decl->kind == Decl::Kind::kStruct;
}

const Type* Typespace::Creator::CreateBoxType() {
  if (!EnsureNumberOfLayoutParams(1))
    return nullptr;

  const Type* boxed_type = nullptr;
  if (!resolver_->ResolveParamAsType(layout_, parameters_.items[0], &boxed_type))
    return nullptr;
  if (!IsStruct(boxed_type)) {
    Fail(ErrCannotBeBoxed, parameters_.items[0]->span, boxed_type->name);
    return nullptr;
  }
  const auto* inner = static_cast<const IdentifierType*>(boxed_type);
  if (inner->nullability == types::Nullability::kNullable) {
    Fail(ErrBoxedTypeCannotBeNullable, parameters_.items[0]->span);
    return nullptr;
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

  out_params_->boxed_type_resolved = boxed_type;
  out_params_->boxed_type_raw = parameters_.items[0]->AsTypeCtor();

  BoxType type(layout_.target_name(), boxed_type);
  std::unique_ptr<Type> constrained_type;
  type.ApplyConstraints(resolver_, constraints_, layout_, &constrained_type, out_params_);
  return typespace_->Intern(std::move(constrained_type));
}

}  // namespace fidl::flat
