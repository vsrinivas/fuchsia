// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/types.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat/type_resolver.h"
#include "tools/fidl/fidlc/include/fidl/flat/visitor.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"

namespace fidl::flat {

// ZX_HANDLE_SAME_RIGHTS
const HandleRights HandleType::kSameRights = HandleRights(0x80000000);

bool ArrayType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                 const Reference& layout, std::unique_ptr<Type>* out_type,
                                 LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TODO(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1) {
    return resolver->Fail(ErrCannotBeOptional, constraints.items[0]->span,
                          layout.resolved().name());
  }
  if (num_constraints > 1) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          0, num_constraints);
  }
  *out_type = std::make_unique<ArrayType>(name, element_type, element_count);
  return true;
}

const Size VectorBaseType::kMaxSize = Size::Max();

bool VectorBaseType::ResolveSizeAndNullability(TypeResolver* resolver,
                                               const TypeConstraints& constraints,
                                               const Reference& layout,
                                               LayoutInvocation* out_params) {
  size_t num_constraints = constraints.items.size();
  if (num_constraints == 1) {
    TypeResolver::ResolvedConstraint resolved;
    if (!resolver->ResolveConstraintAs(
            constraints.items[0].get(),
            {TypeResolver::ConstraintKind::kSize, TypeResolver::ConstraintKind::kNullability},
            nullptr /* resource_decl */, &resolved)) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[0]->span,
                            layout.resolved().name());
    }
    switch (resolved.kind) {
      case TypeResolver::ConstraintKind::kSize:
        out_params->size_resolved = resolved.value.size;
        out_params->size_raw = constraints.items[0].get();
        break;
      case TypeResolver::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        break;
      default:
        ZX_PANIC("resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // first constraint must be size, followed by optional
    if (!resolver->ResolveSizeBound(constraints.items[0].get(), &out_params->size_resolved))
      return resolver->Fail(ErrCouldNotParseSizeBound, constraints.items[0]->span);
    out_params->size_raw = constraints.items[0].get();
    if (!resolver->ResolveAsOptional(constraints.items[1].get())) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[1]->span,
                            layout.resolved().name());
    }
    out_params->nullability = types::Nullability::kNullable;
  } else if (num_constraints >= 3) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          2, num_constraints);
  }
  return true;
}

bool VectorType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                  const Reference& layout, std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  if (!ResolveSizeAndNullability(resolver, constraints, layout, out_params))
    return false;

  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = out_params->nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied) {
    return resolver->Fail(ErrCannotIndicateOptionalTwice, constraints.span.value(),
                          layout.resolved().name());
  }
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  if (element_count != &kMaxSize && out_params->size_resolved)
    return resolver->Fail(ErrCannotBoundTwice, constraints.span.value(), layout.resolved().name());
  auto merged_size = out_params->size_resolved ? out_params->size_resolved : element_count;

  *out_type = std::make_unique<VectorType>(name, element_type, merged_size, merged_nullability);
  return true;
}

bool StringType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                  const Reference& layout, std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  if (!ResolveSizeAndNullability(resolver, constraints, layout, out_params))
    return false;

  bool is_already_nullable = nullability == types::Nullability::kNullable;
  bool is_nullability_applied = out_params->nullability == types::Nullability::kNullable;
  if (is_already_nullable && is_nullability_applied) {
    return resolver->Fail(ErrCannotIndicateOptionalTwice, constraints.span.value(),
                          layout.resolved().name());
  }
  auto merged_nullability = is_already_nullable || is_nullability_applied
                                ? types::Nullability::kNullable
                                : types::Nullability::kNonnullable;

  if (max_size != &kMaxSize && out_params->size_resolved)
    return resolver->Fail(ErrCannotBoundTwice, constraints.span.value(), layout.resolved().name());
  auto merged_size = out_params->size_resolved ? out_params->size_resolved : max_size;

  *out_type = std::make_unique<StringType>(name, merged_size, merged_nullability);
  return true;
}

bool HandleType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                  const Reference& layout, std::unique_ptr<Type>* out_type,
                                  LayoutInvocation* out_params) const {
  ZX_ASSERT(resource_decl);

  // We need to store this separately from out_params, because out_params doesn't
  // store the raw Constant that gets resolved to a nullability constraint.
  std::optional<SourceSpan> applied_nullability_span;

  size_t num_constraints = constraints.items.size();
  if (num_constraints == 0) {
    // no constraints: set to default subtype below
  } else if (num_constraints == 1) {
    // lone constraint can be either subtype or optional
    auto constraint_span = constraints.items[0]->span;
    TypeResolver::ResolvedConstraint resolved;
    if (!resolver->ResolveConstraintAs(constraints.items[0].get(),
                                       {TypeResolver::ConstraintKind::kHandleSubtype,
                                        TypeResolver::ConstraintKind::kNullability},
                                       resource_decl, &resolved))
      return resolver->Fail(ErrUnexpectedConstraint, constraint_span, layout.resolved().name());
    switch (resolved.kind) {
      case TypeResolver::ConstraintKind::kHandleSubtype:
        out_params->subtype_resolved = resolved.value.handle_subtype;
        out_params->subtype_raw = constraints.items[0].get();
        break;
      case TypeResolver::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        ZX_PANIC("resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // the first constraint must be subtype
    auto constraint_span = constraints.items[0]->span;
    uint32_t obj_type = 0;
    if (!resolver->ResolveAsHandleSubtype(resource_decl, constraints.items[0].get(), &obj_type))
      return resolver->Fail(ErrUnexpectedConstraint, constraint_span, layout.resolved().name());
    out_params->subtype_resolved = obj_type;
    out_params->subtype_raw = constraints.items[0].get();

    // the second constraint can either be rights or optional
    constraint_span = constraints.items[1]->span;
    TypeResolver::ResolvedConstraint resolved;
    if (!resolver->ResolveConstraintAs(constraints.items[1].get(),
                                       {TypeResolver::ConstraintKind::kHandleRights,
                                        TypeResolver::ConstraintKind::kNullability},
                                       resource_decl, &resolved))
      return resolver->Fail(ErrUnexpectedConstraint, constraint_span, layout.resolved().name());
    switch (resolved.kind) {
      case TypeResolver::ConstraintKind::kHandleRights:
        out_params->rights_resolved = resolved.value.handle_rights;
        out_params->rights_raw = constraints.items[1].get();
        break;
      case TypeResolver::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        ZX_PANIC("resolved to wrong constraint kind");
    }
  } else if (num_constraints == 3) {
    // no degrees of freedom: must be subtype, followed by rights, then optional
    uint32_t obj_type = 0;
    if (!resolver->ResolveAsHandleSubtype(resource_decl, constraints.items[0].get(), &obj_type)) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[0]->span,
                            layout.resolved().name());
    }
    out_params->subtype_resolved = obj_type;
    out_params->subtype_raw = constraints.items[0].get();
    const HandleRights* rights = nullptr;
    if (!resolver->ResolveAsHandleRights(resource_decl, constraints.items[1].get(), &rights)) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[1]->span,
                            layout.resolved().name());
    }
    out_params->rights_resolved = rights;
    out_params->rights_raw = constraints.items[1].get();
    if (!resolver->ResolveAsOptional(constraints.items[2].get())) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[2]->span,
                            layout.resolved().name());
    }
    out_params->nullability = types::Nullability::kNullable;
    applied_nullability_span = constraints.items[2]->span;
  } else {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          3, num_constraints);
  }

  bool has_obj_type = subtype != types::HandleSubtype::kHandle;
  if (has_obj_type && out_params->subtype_resolved) {
    return resolver->Fail(ErrCannotConstrainTwice, out_params->subtype_raw->span,
                          layout.resolved().name());
  }
  // TODO(fxbug.dev/64629): We need to allow setting a default obj_type in
  // resource_definition declarations rather than hard-coding.
  uint32_t merged_obj_type = obj_type;
  if (out_params->subtype_resolved) {
    merged_obj_type = out_params->subtype_resolved.value();
  }

  bool has_nullability = nullability == types::Nullability::kNullable;
  if (has_nullability && out_params->nullability == types::Nullability::kNullable) {
    return resolver->Fail(ErrCannotIndicateOptionalTwice, applied_nullability_span.value(),
                          layout.resolved().name());
  }
  auto merged_nullability =
      has_nullability || out_params->nullability == types::Nullability::kNullable
          ? types::Nullability::kNullable
          : types::Nullability::kNonnullable;

  bool has_rights = rights != &kSameRights;
  if (has_rights && out_params->rights_resolved) {
    return resolver->Fail(ErrCannotConstrainTwice, out_params->rights_raw->span,
                          layout.resolved().name());
  }
  auto merged_rights = rights;
  if (out_params->rights_resolved) {
    merged_rights = out_params->rights_resolved;
  }

  *out_type = std::make_unique<HandleType>(name, resource_decl, merged_obj_type, merged_rights,
                                           merged_nullability);
  return true;
}

bool TransportSideType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                         const Reference& layout, std::unique_ptr<Type>* out_type,
                                         LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();

  // We need to store this separately from out_params, because out_params doesn't
  // store the raw Constant that gets resolved to a nullability constraint.
  std::optional<SourceSpan> applied_nullability_span;

  if (num_constraints == 1) {
    // could either be a protocol or optional
    auto constraint_span = constraints.items[0]->span;
    TypeResolver::ResolvedConstraint resolved;
    if (!resolver->ResolveConstraintAs(
            constraints.items[0].get(),
            {TypeResolver::ConstraintKind::kProtocol, TypeResolver::ConstraintKind::kNullability},
            /* resource_decl */ nullptr, &resolved))
      return resolver->Fail(ErrUnexpectedConstraint, constraint_span, layout.resolved().name());
    switch (resolved.kind) {
      case TypeResolver::ConstraintKind::kProtocol:
        out_params->protocol_decl = resolved.value.protocol_decl;
        out_params->protocol_decl_raw = constraints.items[0].get();
        break;
      case TypeResolver::ConstraintKind::kNullability:
        out_params->nullability = types::Nullability::kNullable;
        applied_nullability_span = constraint_span;
        break;
      default:
        ZX_PANIC("resolved to wrong constraint kind");
    }
  } else if (num_constraints == 2) {
    // first constraint must be protocol
    if (!resolver->ResolveAsProtocol(constraints.items[0].get(), &out_params->protocol_decl)) {
      return resolver->Fail(ErrMustBeAProtocol, constraints.items[0]->span,
                            layout.resolved().name());
    }
    out_params->protocol_decl_raw = constraints.items[0].get();

    // second constraint must be optional
    if (!resolver->ResolveAsOptional(constraints.items[1].get())) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[1]->span,
                            layout.resolved().name());
    }
    out_params->nullability = types::Nullability::kNullable;
    applied_nullability_span = constraints.items[1]->span;
  } else if (num_constraints > 2) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          2, num_constraints);
  }

  if (protocol_decl && out_params->protocol_decl) {
    return resolver->Fail(ErrCannotConstrainTwice, constraints.items[0]->span,
                          layout.resolved().name());
  }
  if (!protocol_decl && !out_params->protocol_decl) {
    return resolver->Fail(ErrProtocolConstraintRequired, layout.span(), layout.resolved().name());
  }
  const Decl* merged_protocol = protocol_decl;
  if (out_params->protocol_decl)
    merged_protocol = out_params->protocol_decl;

  const Attribute* transport_attribute =
      static_cast<const Protocol*>(merged_protocol)->attributes->Get("transport");
  std::string_view transport("Channel");
  if (transport_attribute) {
    auto arg = (transport_attribute->compiled)
                   ? transport_attribute->GetArg(AttributeArg::kDefaultAnonymousName)
                   : transport_attribute->GetStandaloneAnonymousArg();
    std::string_view quoted_transport =
        static_cast<const LiteralConstant*>(arg->value.get())->literal->span().data();
    // Remove quotes around the transport.
    transport = quoted_transport.substr(1, quoted_transport.size() - 2);
  }

  bool has_nullability = nullability == types::Nullability::kNullable;
  if (has_nullability && out_params->nullability == types::Nullability::kNullable) {
    return resolver->Fail(ErrCannotIndicateOptionalTwice, applied_nullability_span.value(),
                          layout.resolved().name());
  }
  auto merged_nullability =
      has_nullability || out_params->nullability == types::Nullability::kNullable
          ? types::Nullability::kNullable
          : types::Nullability::kNonnullable;

  *out_type = std::make_unique<TransportSideType>(name, merged_protocol, merged_nullability, end,
                                                  transport);
  return true;
}

IdentifierType::IdentifierType(TypeDecl* type_decl, types::Nullability nullability)
    : Type(type_decl->name, Kind::kIdentifier, nullability), type_decl(type_decl) {}

bool IdentifierType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                      const Reference& layout, std::unique_ptr<Type>* out_type,
                                      LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  switch (type_decl->kind) {
    // These types have no allowed constraints
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kTable:
      // assume that a lone constraint was an attempt at specifying `optional` and provide a more
      // specific error
      // TODO(fxbug.dev/75112): actually try to compile the optional constraint
      if (num_constraints == 1) {
        return resolver->Fail(ErrCannotBeOptional, constraints.items[0]->span,
                              layout.resolved().name());
      }
      if (num_constraints > 1) {
        return resolver->Fail(ErrTooManyConstraints, constraints.span.value(),
                              layout.resolved().name(), 0, num_constraints);
      }
      break;
    case Decl::Kind::kStruct:
      if (resolver->experimental_flags().IsFlagEnabled(
              ExperimentalFlags::Flag::kNoOptionalStructs)) {
        // Structs are nullable in the sense that they can be boxed. But we are
        // disallowing optional to be used on struct.
        // Assume first constraint is `optional`.
        if (num_constraints == 1) {
          return resolver->Fail(ErrStructCannotBeOptional, constraints.items[0]->span,
                                layout.resolved().name());
        }
      }

      // This is always an error
      if (num_constraints > 1) {
        return resolver->Fail(ErrTooManyConstraints, constraints.span.value(),
                              layout.resolved().name(), 0, num_constraints);
      }
      break;

    // These types have one allowed constraint (`optional`). For aliases,
    // we need to allow the possibility that the concrete type does allow `optional`,
    // if it doesn't the Type itself will catch the error.
    case Decl::Kind::kAlias:
    case Decl::Kind::kUnion:
      if (num_constraints > 1) {
        return resolver->Fail(ErrTooManyConstraints, constraints.span.value(),
                              layout.resolved().name(), 1, num_constraints);
      }
      break;
    // Currently, we are disallowing optional on new-types. And since new-types are semi-opaque
    // wrappers around types, we need not bother about other constraints. So no constraints for
    // you, new-types!
    case Decl::Kind::kNewType:
      if (num_constraints > 0) {
        return resolver->Fail(ErrNewTypeCannotHaveConstraint, constraints.span.value(),
                              layout.resolved().name());
      }
      break;
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
      // Cannot have const: entries for constants do not exist in the typespace, so
      // they're caught earlier.
      // Cannot have resource: resource types should have resolved to the HandleTypeTemplate
      ZX_PANIC("unexpected identifier type decl kind");
    // TODO(fxbug.dev/75837):
    // These can't be used as types. This will be caught later, in VerifyTypeCategory.
    case Decl::Kind::kService:
    case Decl::Kind::kProtocol:
      break;
  }

  types::Nullability applied_nullability = types::Nullability::kNonnullable;
  if (num_constraints == 1) {
    // must be optional
    if (!resolver->ResolveAsOptional(constraints.items[0].get())) {
      return resolver->Fail(ErrUnexpectedConstraint, constraints.items[0]->span,
                            layout.resolved().name());
    }
    applied_nullability = types::Nullability::kNullable;
  }

  if (nullability == types::Nullability::kNullable &&
      applied_nullability == types::Nullability::kNullable) {
    return resolver->Fail(ErrCannotIndicateOptionalTwice, constraints.span.value(),
                          layout.resolved().name());
  }
  auto merged_nullability = nullability;
  if (applied_nullability == types::Nullability::kNullable)
    merged_nullability = applied_nullability;

  out_params->nullability = applied_nullability;
  *out_type = std::make_unique<IdentifierType>(type_decl, merged_nullability);
  return true;
}

bool BoxType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                               const Reference& layout, std::unique_ptr<Type>* out_type,
                               LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TODO(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1) {
    return resolver->Fail(ErrBoxCannotBeOptional, constraints.items[0]->span);
  }
  if (num_constraints > 1) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          0, num_constraints);
  }
  *out_type = std::make_unique<BoxType>(name, boxed_type);
  return true;
}

bool UntypedNumericType::ApplyConstraints(TypeResolver* resolver,
                                          const TypeConstraints& constraints,
                                          const Reference& layout, std::unique_ptr<Type>* out_type,
                                          LayoutInvocation* out_params) const {
  ZX_PANIC("should not have untyped numeric here");
}

uint32_t PrimitiveType::SubtypeSize(types::PrimitiveSubtype subtype) {
  switch (subtype) {
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kUint8:
    case types::PrimitiveSubtype::kZxUchar:
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
    case types::PrimitiveSubtype::kZxUsize:
    case types::PrimitiveSubtype::kZxUintptr:
      return 8u;
  }
}

bool PrimitiveType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                     const Reference& layout, std::unique_ptr<Type>* out_type,
                                     LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TODO(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1) {
    return resolver->Fail(ErrCannotBeOptional, constraints.items[0]->span,
                          layout.resolved().name());
  }
  if (num_constraints > 1) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          0, num_constraints);
  }
  if ((subtype == types::PrimitiveSubtype::kZxUsize ||
       subtype == types::PrimitiveSubtype::kZxUintptr ||
       subtype == types::PrimitiveSubtype::kZxUchar) &&
      !resolver->experimental_flags().IsFlagEnabled(ExperimentalFlags::Flag::kZxCTypes)) {
    return resolver->Fail(ErrExperimentalZxCTypesDisallowed, layout.span(),
                          layout.resolved().name());
  }
  *out_type = std::make_unique<PrimitiveType>(name, subtype);
  return true;
}

bool InternalType::ApplyConstraints(TypeResolver* resolver, const TypeConstraints& constraints,
                                    const Reference& layout, std::unique_ptr<Type>* out_type,
                                    LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  // assume that a lone constraint was an attempt at specifying `optional` and provide a more
  // specific error
  // TODO(fxbug.dev/75112): actually try to compile the optional constraint
  if (num_constraints == 1) {
    return resolver->Fail(ErrCannotBeOptional, constraints.items[0]->span,
                          layout.resolved().name());
  }
  if (num_constraints > 1) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          0, num_constraints);
  }
  *out_type = std::make_unique<InternalType>(name, subtype);
  return true;
}

bool ZxExperimentalPointerType::ApplyConstraints(TypeResolver* resolver,
                                                 const TypeConstraints& constraints,
                                                 const Reference& layout,
                                                 std::unique_ptr<Type>* out_type,
                                                 LayoutInvocation* out_params) const {
  size_t num_constraints = constraints.items.size();
  if (num_constraints > 1) {
    return resolver->Fail(ErrTooManyConstraints, constraints.span.value(), layout.resolved().name(),
                          0, num_constraints);
  }
  if (!resolver->experimental_flags().IsFlagEnabled(ExperimentalFlags::Flag::kZxCTypes)) {
    return resolver->Fail(ErrExperimentalZxCTypesDisallowed, constraints.span.value(),
                          layout.resolved().name());
  }
  *out_type = std::make_unique<ZxExperimentalPointerType>(name, pointee_type);
  return true;
}

types::Resourceness Type::Resourceness() const {
  switch (this->kind) {
    case Type::Kind::kPrimitive:
    case Type::Kind::kInternal:
    case Type::Kind::kString:
      return types::Resourceness::kValue;
    case Type::Kind::kHandle:
    case Type::Kind::kTransportSide:
      return types::Resourceness::kResource;
    case Type::Kind::kArray:
      return static_cast<const ArrayType*>(this)->element_type->Resourceness();
    case Type::Kind::kVector:
      return static_cast<const VectorType*>(this)->element_type->Resourceness();
    case Type::Kind::kZxExperimentalPointer:
      return static_cast<const ZxExperimentalPointerType*>(this)->pointee_type->Resourceness();
    case Type::Kind::kIdentifier:
      break;
    case Type::Kind::kBox:
      return static_cast<const BoxType*>(this)->boxed_type->Resourceness();
    case Type::Kind::kUntypedNumeric:
      ZX_PANIC("should not have untyped numeric here");
  }

  const auto* decl = static_cast<const IdentifierType*>(this)->type_decl;

  switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
      return types::Resourceness::kValue;
    case Decl::Kind::kProtocol:
      return types::Resourceness::kResource;
    case Decl::Kind::kStruct:
      ZX_ASSERT_MSG(decl->compiled, "accessing resourceness of not-yet-compiled struct");
      return static_cast<const Struct*>(decl)->resourceness.value();
    case Decl::Kind::kTable:
      return static_cast<const Table*>(decl)->resourceness;
    case Decl::Kind::kUnion:
      ZX_ASSERT_MSG(decl->compiled, "accessing resourceness of not-yet-compiled union");
      return static_cast<const Union*>(decl)->resourceness.value();
    case Decl::Kind::kNewType: {
      const auto* new_type = static_cast<const NewType*>(decl);
      const auto* underlying_type = new_type->type_ctor->type;
      ZX_ASSERT_MSG(underlying_type, "accessing resourceness of not-yet-compiled new-type");
      return underlying_type->Resourceness();
    }
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
    case Decl::Kind::kService:
    case Decl::Kind::kAlias:
      ZX_PANIC("unexpected kind");
  }
}

std::any ArrayType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any BoxType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any HandleType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any IdentifierType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any PrimitiveType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any InternalType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any StringType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any TransportSideType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any VectorType::AcceptAny(VisitorAny* visitor) const { return visitor->Visit(*this); }
std::any ZxExperimentalPointerType::AcceptAny(VisitorAny* visitor) const {
  return visitor->Visit(*this);
}

std::any UntypedNumericType::AcceptAny(VisitorAny* visitor) const {
  ZX_PANIC("should not have untyped numeric here");
}

}  // namespace fidl::flat
