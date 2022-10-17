// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/verify_steps.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat/attribute_schema.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"

namespace fidl::flat {

void VerifyResourcenessStep::RunImpl() {
  for (const auto& [name, decl] : library()->declarations.all) {
    VerifyDecl(decl);
  }
}

void VerifyResourcenessStep::VerifyDecl(const Decl* decl) {
  ZX_ASSERT_MSG(decl->compiled, "verification must happen after compilation of decls");
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
    case Type::Kind::kInternal: {
      switch (static_cast<const InternalType*>(type)->subtype) {
        case fidl::types::InternalSubtype::kTransportErr:
          return types::Resourceness::kValue;
      }
    }
    case Type::Kind::kHandle:
    case Type::Kind::kTransportSide:
      return types::Resourceness::kResource;
    case Type::Kind::kArray:
      return EffectiveResourceness(static_cast<const ArrayType*>(type)->element_type);
    case Type::Kind::kVector:
      return EffectiveResourceness(static_cast<const VectorType*>(type)->element_type);
    case Type::Kind::kZxExperimentalPointer:
      return EffectiveResourceness(
          static_cast<const ZxExperimentalPointerType*>(type)->pointee_type);
    case Type::Kind::kIdentifier:
      break;
    case Type::Kind::kBox:
      return EffectiveResourceness(static_cast<const BoxType*>(type)->boxed_type);
    case Type::Kind::kUntypedNumeric:
      ZX_PANIC("should not have untyped numeric here");
  }

  const auto* decl = static_cast<const IdentifierType*>(type)->type_decl;

  switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
      return types::Resourceness::kValue;
    case Decl::Kind::kNewType: {
      const auto* new_type = static_cast<const NewType*>(decl);
      return EffectiveResourceness(new_type->type_ctor->type);
    }
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
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
    case Decl::Kind::kAlias:
      ZX_PANIC("unexpected kind");
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
      ZX_PANIC("unexpected kind");
  }

  effective_resourceness_[decl] = types::Resourceness::kValue;
  return types::Resourceness::kValue;
}

void VerifyHandleTransportCompatibilityStep::RunImpl() {
  for (const auto& protocol : library()->declarations.protocols) {
    VerifyProtocol(protocol.get());
  }
}

void VerifyHandleTransportCompatibilityStep::VerifyProtocol(const Protocol* protocol) {
  std::string_view transport_name = "Channel";
  Attribute* transport_attribute = protocol->attributes->Get("transport");
  if (transport_attribute != nullptr) {
    auto arg = transport_attribute->GetArg(AttributeArg::kDefaultAnonymousName);
    std::string_view quoted_transport =
        static_cast<const LiteralConstant*>(arg->value.get())->literal->span().data();
    // Remove quotes around the transport.
    transport_name = quoted_transport.substr(1, quoted_transport.size() - 2);
  }
  std::optional<Transport> transport = Transport::FromTransportName(transport_name);
  if (!transport.has_value()) {
    return;
  }

  for (auto& method : protocol->methods) {
    if (method.maybe_request) {
      std::set<const Decl*> seen;
      CheckHandleTransportUsages(method.maybe_request->type, transport.value(), protocol,
                                 method.name, seen);
    }
    if (method.maybe_response) {
      std::set<const Decl*> seen;
      CheckHandleTransportUsages(method.maybe_response->type, transport.value(), protocol,
                                 method.name, seen);
    }
  }
}

void VerifyHandleTransportCompatibilityStep::CheckHandleTransportUsages(
    const Type* type, const Transport& transport, const Protocol* protocol, SourceSpan source_span,
    std::set<const Decl*>& seen) {
  switch (type->kind) {
    case Type::Kind::kUntypedNumeric:
    case Type::Kind::kPrimitive:
    case Type::Kind::kString:
      return;
    case Type::Kind::kInternal: {
      switch (static_cast<const InternalType*>(type)->subtype) {
        case fidl::types::InternalSubtype::kTransportErr:
          return;
      }
    }
    case Type::Kind::kArray:
      return CheckHandleTransportUsages(static_cast<const ArrayType*>(type)->element_type,
                                        transport, protocol, source_span, seen);
    case Type::Kind::kVector:
      return CheckHandleTransportUsages(static_cast<const VectorType*>(type)->element_type,
                                        transport, protocol, source_span, seen);
    case Type::Kind::kZxExperimentalPointer:
      return CheckHandleTransportUsages(
          static_cast<const ZxExperimentalPointerType*>(type)->pointee_type, transport, protocol,
          source_span, seen);
    case Type::Kind::kBox:
      return CheckHandleTransportUsages(static_cast<const BoxType*>(type)->boxed_type, transport,
                                        protocol, source_span, seen);
    case Type::Kind::kHandle: {
      const Resource* resource = static_cast<const HandleType*>(type)->resource_decl;
      std::string handle_name =
          LibraryName(resource->name.library()->name, ".") + "." + resource->GetName();
      std::optional<HandleClass> handle_class = HandleClassFromName(handle_name);
      if (!handle_class.has_value() || !transport.IsCompatible(handle_class.value())) {
        Fail(ErrHandleUsedInIncompatibleTransport, source_span, handle_name, transport.name,
             protocol);
      }
      return;
    }
    case Type::Kind::kTransportSide: {
      std::string_view transport_name =
          static_cast<const TransportSideType*>(type)->protocol_transport;
      Transport transport_side_transport = Transport::FromTransportName(transport_name).value();
      if (!transport_side_transport.handle_class.has_value() ||
          !transport.IsCompatible(transport_side_transport.handle_class.value())) {
        Fail(ErrTransportEndUsedInIncompatibleTransport, source_span, transport_name,
             transport.name, protocol);
      }
      return;
    }
    case Type::Kind::kIdentifier:
      break;
  }

  const TypeDecl* decl = static_cast<const IdentifierType*>(type)->type_decl;

  // Break loops in recursive types.
  if (seen.find(decl) != seen.end()) {
    return;
  }
  seen.insert(decl);

  switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
      return;
    case Decl::Kind::kProtocol:
    case Decl::Kind::kBuiltin:
    case Decl::Kind::kConst:
    case Decl::Kind::kResource:
    case Decl::Kind::kAlias:
    case Decl::Kind::kService:
      ZX_PANIC("unexpected kind");

    case Decl::Kind::kNewType: {
      const auto* new_type = static_cast<const NewType*>(decl);
      CheckHandleTransportUsages(new_type->type_ctor->type, transport, protocol, source_span, seen);
      return;
    }
    case Decl::Kind::kStruct: {
      const Struct* s = static_cast<const Struct*>(decl);
      for (auto& member : s->members) {
        CheckHandleTransportUsages(member.type_ctor->type, transport, protocol, member.name, seen);
      }
      return;
    }
    case Decl::Kind::kTable: {
      const Table* t = static_cast<const Table*>(decl);
      for (auto& member : t->members) {
        if (member.maybe_used != nullptr) {
          CheckHandleTransportUsages(member.maybe_used->type_ctor->type, transport, protocol,
                                     member.maybe_used->name, seen);
        }
      }
      return;
    }
    case Decl::Kind::kUnion: {
      const Union* u = static_cast<const Union*>(decl);
      for (auto& member : u->members) {
        if (member.maybe_used != nullptr) {
          CheckHandleTransportUsages(member.maybe_used->type_ctor->type, transport, protocol,
                                     member.maybe_used->name, seen);
        }
      }
      return;
    }
  }
}

void VerifyAttributesStep::RunImpl() {
  library()->TraverseElements([&](Element* element) { VerifyAttributes(element); });
}

void VerifyAttributesStep::VerifyAttributes(const Element* element) {
  for (const auto& attribute : element->attributes->attributes) {
    const AttributeSchema& schema = all_libraries()->RetrieveAttributeSchema(attribute.get());
    schema.Validate(reporter(), attribute.get(), element);
  }
}

void VerifyDependenciesStep::RunImpl() {
  library()->dependencies.VerifyAllDependenciesWereUsed(*library(), reporter());
}

void VerifyInlineSizeStep::RunImpl() {
  // This limit exists so that coding tables can use uint16_t for sizes.
  auto limit = std::numeric_limits<uint16_t>::max();
  for (auto& struct_decl : library()->declarations.structs) {
    auto inline_size = struct_decl->typeshape(WireFormat::kV1NoEe).inline_size;
    if (inline_size > limit) {
      Fail(ErrInlineSizeExceedsLimit, struct_decl->name.span().value(), struct_decl->name,
           inline_size, limit);
    }
  }
}

void VerifyOpenInteractionsStep::RunImpl() {
  for (const auto& protocol : library()->declarations.protocols) {
    VerifyProtocolOpenness(*protocol);
  }
}

void VerifyOpenInteractionsStep::VerifyProtocolOpenness(const Protocol& protocol) {
  ZX_ASSERT_MSG(protocol.compiled, "verification must happen after compilation of decls");

  for (const auto& composed : protocol.composed_protocols) {
    auto target = composed.reference.resolved().element();
    ZX_ASSERT_MSG(target->kind == Element::Kind::kProtocol, "composed protocol not a protocol");
    auto composed_protocol = static_cast<const Protocol*>(target);
    if (!IsAllowedComposition(protocol.openness, composed_protocol->openness)) {
      Fail(ErrComposedProtocolTooOpen, composed.reference.span(), protocol.openness, protocol.name,
           composed_protocol->openness, composed_protocol->name);
    }
  }

  for (const auto& method : protocol.methods) {
    if (method.strictness == types::Strictness::kFlexible) {
      if (method.has_request && method.has_response) {
        // This is a two-way method, so it must be in an open protocol.
        if (protocol.openness != types::Openness::kOpen) {
          Fail(ErrFlexibleTwoWayMethodRequiresOpenProtocol, method.name, protocol.openness);
        }
      } else {
        // This is an event or one-way method, so it can be in either an open
        // protocol or an ajar protocol.
        if (protocol.openness == types::Openness::kClosed) {
          Fail(ErrFlexibleOneWayMethodInClosedProtocol, method.name,
               method.has_request ? "one-way method" : "event");
        }
      }
    }
  }
}

bool VerifyOpenInteractionsStep::IsAllowedComposition(types::Openness composing,
                                                      types::Openness composed) {
  switch (composing) {
    case types::Openness::kOpen:
      // Open protocol can compose any other protocol.
      return true;
    case types::Openness::kAjar:
      // Ajar protocols can compose anything that isn't open.
      return composed != types::Openness::kOpen;
    case types::Openness::kClosed:
      // Closed protocol can only compose another closed protocol.
      return composed == types::Openness::kClosed;
  }
}

}  // namespace fidl::flat
