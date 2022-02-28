// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/resolve_step.h"

#include "fidl/diagnostics.h"

namespace fidl::flat {

void ResolveStep::RunImpl() {
  ResolveForContext();
  library()->TraverseElements([&](Element* element) { ResolveElement(element); });
}

void ResolveStep::ResolveForContext() {
  // We need to resolve the type constructors of resource properties early so
  // that ConstraintContext can get their targets. Note that this only matters
  // for contextual constraints in the same library. If the resource is in
  // another library, it will already have been resolved and compiled.
  for (auto& resource : library()->resource_declarations) {
    for (auto& property : resource->properties) {
      ResolveTypeConstructor(property.type_ctor.get());
    }
  }
}

// static
const ResolveStep::Context ResolveStep::Context::kNone = {};

// static
ResolveStep::Context ResolveStep::ConstraintContext(const TypeConstructor* type_ctor) {
  if (!type_ctor->layout.IsResolved()) {
    return {};
  }
  auto target = type_ctor->layout.target();
  if (target->kind != Element::Kind::kResource) {
    return {};
  }
  auto subtype = static_cast<Resource*>(target)->LookupProperty("subtype");
  if (!subtype) {
    return {};
  }
  if (!subtype->type_ctor->layout.IsResolved()) {
    return {};
  }
  auto subtype_target = subtype->type_ctor->layout.target();
  if (subtype_target->kind != Element::Kind::kEnum) {
    return {};
  }
  return Context{.maybe_resource_subtype = static_cast<Enum*>(subtype_target)};
}

void ResolveStep::ResolveElement(Element* element) {
  for (auto& attribute : element->attributes->attributes) {
    for (auto& arg : attribute->args) {
      ResolveConstant(arg->value.get());
    }
  }
  switch (element->kind) {
    case Element::Kind::kTypeAlias: {
      auto alias_decl = static_cast<TypeAlias*>(element);
      ResolveTypeConstructor(alias_decl->partial_type_ctor.get());
      break;
    }
    case Element::Kind::kConst: {
      auto const_decl = static_cast<Const*>(element);
      ResolveTypeConstructor(const_decl->type_ctor.get());
      ResolveConstant(const_decl->value.get());
      break;
    }
    case Element::Kind::kBits: {
      auto bits_decl = static_cast<Bits*>(element);
      ResolveTypeConstructor(bits_decl->subtype_ctor.get());
      break;
    }
    case Element::Kind::kBitsMember: {
      auto bits_member = static_cast<Bits::Member*>(element);
      ResolveConstant(bits_member->value.get());
      break;
    }
    case Element::Kind::kEnum: {
      auto enum_decl = static_cast<Enum*>(element);
      ResolveTypeConstructor(enum_decl->subtype_ctor.get());
      break;
    }
    case Element::Kind::kEnumMember: {
      auto enum_member = static_cast<Enum::Member*>(element);
      ResolveConstant(enum_member->value.get());
      break;
    }
    case Element::Kind::kStructMember: {
      auto struct_member = static_cast<Struct::Member*>(element);
      ResolveTypeConstructor(struct_member->type_ctor.get());
      if (auto& constant = struct_member->maybe_default_value) {
        ResolveConstant(constant.get());
      }
      break;
    }
    case Element::Kind::kTableMember: {
      auto table_member = static_cast<Table::Member*>(element);
      if (auto& used = table_member->maybe_used) {
        ResolveTypeConstructor(used->type_ctor.get());
      }
      break;
    }
    case Element::Kind::kUnionMember: {
      auto union_member = static_cast<Union::Member*>(element);
      if (auto& used = union_member->maybe_used) {
        ResolveTypeConstructor(used->type_ctor.get());
      }
      break;
    }
    case Element::Kind::kProtocolCompose: {
      auto composed_protocol = static_cast<Protocol::ComposedProtocol*>(element);
      ResolveReference(composed_protocol->reference);
      break;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<Protocol::Method*>(element);
      if (auto& type_ctor = method->maybe_request) {
        ResolveTypeConstructor(type_ctor.get());
      }
      if (auto& type_ctor = method->maybe_response) {
        ResolveTypeConstructor(type_ctor.get());
      }
      break;
    }
    case Element::Kind::kServiceMember: {
      auto service_member = static_cast<Service::Member*>(element);
      ResolveTypeConstructor(service_member->type_ctor.get());
      break;
    }
    case Element::Kind::kResource: {
      auto resource_decl = static_cast<Resource*>(element);
      ResolveTypeConstructor(resource_decl->subtype_ctor.get());
      break;
    }
    case Element::Kind::kResourceProperty:
      // The resource property's type constructor is resolved earlier, in
      // ResolveForContext().
      break;
    case Element::Kind::kBuiltin:
    case Element::Kind::kLibrary:
    case Element::Kind::kProtocol:
    case Element::Kind::kService:
    case Element::Kind::kStruct:
    case Element::Kind::kTable:
    case Element::Kind::kUnion:
      break;
  }
}

void ResolveStep::ResolveTypeConstructor(TypeConstructor* type_ctor) {
  ResolveReference(type_ctor->layout);
  for (auto& param : type_ctor->parameters->items) {
    switch (param->kind) {
      case LayoutParameter::kLiteral:
        break;
      case LayoutParameter::kType: {
        auto type_param = static_cast<TypeLayoutParameter*>(param.get());
        ResolveTypeConstructor(type_param->type_ctor.get());
        break;
      }
      case LayoutParameter::kIdentifier: {
        auto identifier_param = static_cast<IdentifierLayoutParameter*>(param.get());
        ResolveReference(identifier_param->reference);
        if (identifier_param->reference.IsResolved()) {
          identifier_param->Disambiguate();
        }
        break;
      }
    }
  }
  for (auto& constraint : type_ctor->constraints->items) {
    ResolveConstant(constraint.get(), ConstraintContext(type_ctor));
  }
}

void ResolveStep::ResolveConstant(Constant* constant, Context context) {
  switch (constant->kind) {
    case Constant::Kind::kLiteral:
      break;
    case Constant::Kind::kIdentifier: {
      auto identifier_constant = static_cast<IdentifierConstant*>(constant);
      ResolveReference(identifier_constant->reference, context);
      break;
    }
    case Constant::Kind::kBinaryOperator: {
      auto binop_constant = static_cast<BinaryOperatorConstant*>(constant);
      ResolveConstant(binop_constant->left_operand.get(), context);
      ResolveConstant(binop_constant->right_operand.get(), context);
      break;
    }
  }
}

namespace {

// Helper methods for looking up a name as a library, decl, or member. The Try*
// methods do not report an error, while the Must* methods do.
class Lookup final : ReporterMixin {
 public:
  Lookup(const Reference& ref, const Library* root_library, const Dependencies& dependencies,
         Reporter* reporter)
      : ReporterMixin(reporter),
        ref_(ref),
        root_library_(root_library),
        dependencies_(dependencies) {}

  const Library* TryLibrary(const std::vector<std::string_view>& name) {
    if (name == root_library_->name) {
      return root_library_;
    }
    auto filename = ref_.span()->source_file().filename();
    return dependencies_.LookupAndMarkUsed(filename, name);
  }

  Decl* TryDecl(const Library* library, std::string_view name) {
    auto iter = library->declarations.find(name);
    return iter == library->declarations.end() ? nullptr : iter->second;
  }

  Decl* MustDecl(const Library* library, std::string_view name) {
    if (auto decl = TryDecl(library, name)) {
      return decl;
    }
    Fail(ErrNameNotFound, ref_.span().value(), name, library->name);
    return nullptr;
  }

  Element* TryMember(Decl* parent, std::string_view name) {
    switch (parent->kind) {
      case Decl::Kind::kBits:
        for (auto& member : static_cast<Bits*>(parent)->members) {
          if (member.name.data() == name) {
            return &member;
          }
        }
        return nullptr;
      case Decl::Kind::kEnum:
        for (auto& member : static_cast<Enum*>(parent)->members) {
          if (member.name.data() == name) {
            return &member;
          }
        }
        return nullptr;
      default:
        return nullptr;
    }
  }

  Element* MustMember(Decl* parent, std::string_view name) {
    switch (parent->kind) {
      case Decl::Kind::kBits:
      case Decl::Kind::kEnum:
        if (auto member = TryMember(parent, name)) {
          return member;
        }
        break;
      default:
        Fail(ErrCannotReferToMember, ref_.span().value(), parent);
        return nullptr;
    }
    Fail(ErrMemberNotFound, ref_.span().value(), parent, name);
    return nullptr;
  }

 private:
  const Reference& ref_;
  const Library* root_library_;
  const Dependencies& dependencies_;
};

}  // namespace

void ResolveStep::ResolveReference(Reference& ref, Context context) {
  // Skip pre-resolved references.
  if (!ref.span()) {
    assert(ref.IsResolved() && "reference without span should be pre-resolved");
    return;
  }

  // Below is an outline of FIDL scoping semantics. We navigate it by moving
  // down and to the right. That is, if a rule succeeds, we proceed to the
  // indented one below it (if there is none, SUCCESS); if a rule fails, we try
  // the same-indentation one below it (if there is none, FAIL).
  //
  // - X
  //     - Resolve X as a decl within the current library.
  //     - Resolve X as a decl within the root library.
  //     - Resolve X as a contextual bits/enum member, if context exists.
  // - X.Y
  //     - Resolve X as a decl within the current library.
  //         - Resolve Y as a member of X.
  //     - Resolve X as a library name or alias.
  //         - Resolve Y as a decl within X.
  // - x.Y.Z where x represents 1+ components
  //     - Resolve x.Y as a library name or alias.
  //         - Resolve Z as a decl within x.Y.
  //     - Resolve x as a library name or alias.
  //         - Resolve Y as a decl within x.
  //             - Resolve Z as a member of Y.
  //
  // Note that if you import libraries A and A.B, you cannot refer to bits/enum
  // member [library A].B.C because it is interpreted as decl [library A.B].C.
  // To do so, you must remove or alias one of the imports. We implement this
  // behavior even if [library A.B].C does not exist, since otherwise
  // introducing it would result in breakage at a distance. For FIDL code that
  // follow the linter naming conventions (lowercase library, CamelCase decl),
  // this will never come up in practice.

  Element* target = nullptr;
  Decl* maybe_parent = nullptr;
  const auto& components = ref.components();
  Lookup lookup(ref, all_libraries()->root_library(), library()->dependencies, reporter());
  switch (components.size()) {
    case 1: {
      if (auto decl = lookup.TryDecl(library(), components[0])) {
        target = decl;
        break;
      }
      if (auto decl = lookup.TryDecl(all_libraries()->root_library(), components[0])) {
        target = decl;
        break;
      }
      if (auto parent = context.maybe_resource_subtype) {
        if (auto member = lookup.TryMember(parent, components[0])) {
          target = member;
          maybe_parent = parent;
          break;
        }
      }
      Fail(ErrNameNotFound, ref.span().value(), components[0], library()->name);
      return;
    }
    case 2: {
      if (auto parent = lookup.TryDecl(library(), components[0])) {
        auto member = lookup.MustMember(parent, components[1]);
        if (!member) {
          return;
        }
        target = member;
        maybe_parent = parent;
        break;
      }
      if (auto dep_library = lookup.TryLibrary({components[0]})) {
        auto decl = lookup.MustDecl(dep_library, components[1]);
        if (!decl) {
          return;
        }
        target = decl;
        break;
      }
      Fail(ErrNameNotFound, ref.span().value(), components[0], library()->name);
      return;
    }
    default: {
      std::vector<std::string_view> long_library_name(components.begin(), components.end() - 1);
      if (auto dep_library = lookup.TryLibrary(long_library_name)) {
        auto decl = lookup.MustDecl(dep_library, components.back());
        if (!decl) {
          return;
        }
        target = decl;
        break;
      }
      std::vector<std::string_view> short_library_name(components.begin(), components.end() - 2);
      if (auto dep_library = lookup.TryLibrary(short_library_name)) {
        auto parent = lookup.MustDecl(dep_library, components[components.size() - 2]);
        if (!parent) {
          return;
        }
        auto member = lookup.MustMember(parent, components.back());
        if (!member) {
          return;
        }
        target = member;
        maybe_parent = parent;
        break;
      }
      Fail(ErrUnknownDependentLibrary, ref.span().value(), long_library_name, short_library_name);
      return;
    }
  }

  assert(target);
  ref.Resolve(target, maybe_parent);
}

}  // namespace fidl::flat
