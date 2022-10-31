// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/type_resolver.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat/compile_step.h"

namespace fidl::flat {

bool TypeResolver::ResolveParamAsType(const Reference& layout,
                                      const std::unique_ptr<LayoutParameter>& param,
                                      const Type** out_type) {
  auto type_ctor = param->AsTypeCtor();
  auto check = reporter()->Checkpoint();
  if (!type_ctor || !ResolveType(type_ctor)) {
    // if there were no errors reported but we couldn't resolve to a type, it must
    // mean that the parameter referred to a non-type, so report a new error here.
    if (check.NoNewErrors()) {
      return Fail(ErrExpectedType, param->span);
    }
    // otherwise, there was an error during the type resolution process, so we
    // should just report that rather than add an extra error here
    return false;
  }
  *out_type = type_ctor->type;
  return true;
}

bool TypeResolver::ResolveParamAsSize(const Reference& layout,
                                      const std::unique_ptr<LayoutParameter>& param,
                                      const Size** out_size) {
  // We could use param->AsConstant() here, leading to code similar to ResolveParamAsType.
  // However, unlike ErrExpectedType, ErrExpectedValueButGotType requires a name to be
  // reported, which would require doing a switch on the parameter kind anyway to find
  // its Name. So we just handle all the cases ourselves from the start.
  switch (param->kind) {
    case LayoutParameter::Kind::kLiteral: {
      auto literal_param = static_cast<LiteralLayoutParameter*>(param.get());
      if (!ResolveSizeBound(literal_param->literal.get(), out_size))
        return Fail(ErrCouldNotResolveSizeBound, literal_param->span);
      break;
    }
    case LayoutParameter::kType: {
      auto type_param = static_cast<TypeLayoutParameter*>(param.get());
      return Fail(ErrExpectedValueButGotType, type_param->span,
                  type_param->type_ctor->layout.resolved().name());
    }
    case LayoutParameter::Kind::kIdentifier: {
      auto ambig_param = static_cast<IdentifierLayoutParameter*>(param.get());
      auto as_constant = ambig_param->AsConstant();
      if (!as_constant) {
        return Fail(ErrExpectedValueButGotType, ambig_param->span,
                    ambig_param->reference.resolved().name());
      }
      if (!ResolveSizeBound(as_constant, out_size)) {
        return Fail(ErrCannotResolveConstantValue, ambig_param->span);
      }
      break;
    }
  }
  ZX_ASSERT(*out_size);
  if ((*out_size)->value == 0)
    return Fail(ErrMustHaveNonZeroSize, param->span, layout.resolved().name());
  return true;
}

bool TypeResolver::ResolveConstraintAs(Constant* constraint,
                                       const std::vector<ConstraintKind>& interpretations,
                                       Resource* resource, ResolvedConstraint* out) {
  for (const auto& constraint_kind : interpretations) {
    out->kind = constraint_kind;
    switch (constraint_kind) {
      case ConstraintKind::kHandleSubtype: {
        ZX_ASSERT_MSG(resource, "must pass resource if trying to resolve to handle subtype");
        if (ResolveAsHandleSubtype(resource, constraint, &out->value.handle_subtype))
          return true;
        break;
      }
      case ConstraintKind::kHandleRights: {
        ZX_ASSERT_MSG(resource, "must pass resource if trying to resolve to handle rights");
        if (ResolveAsHandleRights(resource, constraint, &(out->value.handle_rights)))
          return true;
        break;
      }
      case ConstraintKind::kSize: {
        if (ResolveSizeBound(constraint, &(out->value.size)))
          return true;
        break;
      }
      case ConstraintKind::kNullability: {
        if (ResolveAsOptional(constraint))
          return true;
        break;
      }
      case ConstraintKind::kProtocol: {
        if (ResolveAsProtocol(constraint, &(out->value.protocol_decl)))
          return true;
        break;
      }
    }
  }
  return false;
}

bool TypeResolver::ResolveType(TypeConstructor* type) {
  compile_step_->CompileTypeConstructor(type);
  return type->type != nullptr;
}

bool TypeResolver::ResolveSizeBound(Constant* size_constant, const Size** out_size) {
  return compile_step_->ResolveSizeBound(size_constant, out_size);
}

bool TypeResolver::ResolveAsOptional(Constant* constant) {
  return compile_step_->ResolveAsOptional(constant);
}

bool TypeResolver::ResolveAsHandleSubtype(Resource* resource, Constant* constant,
                                          uint32_t* out_obj_type) {
  return compile_step_->ResolveHandleSubtypeIdentifier(resource, constant, out_obj_type);
}

bool TypeResolver::ResolveAsHandleRights(Resource* resource, Constant* constant,
                                         const HandleRights** out_rights) {
  return compile_step_->ResolveHandleRightsConstant(resource, constant, out_rights);
}

bool TypeResolver::ResolveAsProtocol(const Constant* constant, const Protocol** out_decl) {
  // TODO(fxbug.dev/75112): If/when this method is responsible for reporting errors, the
  // `return false` statements should fail with ErrConstraintMustBeProtocol instead.
  if (constant->kind != Constant::Kind::kIdentifier)
    return false;

  const auto* as_identifier = static_cast<const IdentifierConstant*>(constant);
  const auto* target = as_identifier->reference.resolved().element();
  if (target->kind != Element::Kind::kProtocol)
    return false;
  *out_decl = static_cast<const Protocol*>(target);
  return true;
}

void TypeResolver::CompileDecl(Decl* decl) { compile_step_->CompileDecl(decl); }

std::optional<std::vector<const Decl*>> TypeResolver::GetDeclCycle(const Decl* decl) {
  return compile_step_->GetDeclCycle(decl);
}

}  // namespace fidl::flat
