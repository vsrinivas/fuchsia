// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/verify_steps.h"

#include "fidl/flat/attribute_schema.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

using namespace diagnostics;

void VerifyResourcenessStep::RunImpl() {
  for (const Decl* decl : library_->declaration_order_) {
    VerifyDecl(decl);
  }
}

void VerifyResourcenessStep::VerifyDecl(const Decl* decl) {
  assert(decl->compiled && "verification must happen after compilation of decls");
  switch (decl->kind) {
    case Decl::Kind::kStruct: {
      const auto* struct_decl = static_cast<const Struct*>(decl);
      if (struct_decl->resourceness == types::Resourceness::kValue) {
        for (const auto& member : struct_decl->members) {
          if (EffectiveResourceness(member.type_ctor->type) == types::Resourceness::kResource) {
            Fail(ErrTypeMustBeResource, struct_decl->name.span().value(), struct_decl->name,
                 member.name.data(), "struct", struct_decl->name);
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
            if (EffectiveResourceness(used.type_ctor->type) == types::Resourceness::kResource) {
              Fail(ErrTypeMustBeResource, table_decl->name.span().value(), table_decl->name,
                   used.name.data(), "table", table_decl->name);
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
            if (EffectiveResourceness(used.type_ctor->type) == types::Resourceness::kResource) {
              Fail(ErrTypeMustBeResource, union_decl->name.span().value(), union_decl->name,
                   used.name.data(), "union", union_decl->name);
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

types::Resourceness VerifyResourcenessStep::EffectiveResourceness(const Type* type) {
  switch (type->kind) {
    case Type::Kind::kPrimitive:
    case Type::Kind::kString:
      return types::Resourceness::kValue;
    case Type::Kind::kHandle:
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
    case Type::Kind::kUntypedNumeric:
      assert(false && "compiler bug: should not have untyped numeric here");
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
        if (EffectiveResourceness(member.type_ctor->type) == types::Resourceness::kResource) {
          effective_resourceness_[decl] = types::Resourceness::kResource;
          return types::Resourceness::kResource;
        }
      }
      break;
    case Decl::Kind::kTable:
      for (const auto& member : static_cast<const Table*>(decl)->members) {
        const auto& used = member.maybe_used;
        if (used &&
            EffectiveResourceness(used->type_ctor->type) == types::Resourceness::kResource) {
          effective_resourceness_[decl] = types::Resourceness::kResource;
          return types::Resourceness::kResource;
        }
      }
      break;
    case Decl::Kind::kUnion:
      for (const auto& member : static_cast<const Union*>(decl)->members) {
        const auto& used = member.maybe_used;
        if (used &&
            EffectiveResourceness(used->type_ctor->type) == types::Resourceness::kResource) {
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

void VerifyAttributesStep::RunImpl() {
  library_->TraverseElements([&](Element* element) { VerifyAttributes(element); });
}

void VerifyAttributesStep::VerifyAttributes(const Element* element) {
  for (const auto& attribute : element->attributes->attributes) {
    const AttributeSchema& schema =
        library_->all_libraries_->RetrieveAttributeSchema(reporter(), attribute.get());
    schema.Validate(reporter(), attribute.get(), element);
  }
}

void VerifyDependenciesStep::RunImpl() {
  library_->dependencies_.VerifyAllDependenciesWereUsed(*library_, reporter());
}

void VerifyInlineSizeStep::RunImpl() {
  for (const Decl* decl : library_->declaration_order_) {
    if (decl->kind == Decl::Kind::kStruct) {
      auto struct_decl = static_cast<const Struct*>(decl);
      if (struct_decl->typeshape(WireFormat::kV1NoEe).inline_size >= 65536) {
        Fail(ErrInlineSizeExceeds64k, struct_decl->name.span().value());
      }
    }
  }
}

}  // namespace fidl::flat
