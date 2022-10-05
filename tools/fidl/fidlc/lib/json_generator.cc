// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/json_generator.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/diagnostic_types.h"
#include "tools/fidl/fidlc/include/fidl/flat/name.h"
#include "tools/fidl/fidlc/include/fidl/flat/types.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/names.h"
#include "tools/fidl/fidlc/include/fidl/types.h"

namespace fidl {

void JSONGenerator::Generate(SourceSpan value) { EmitString(value.data()); }

void JSONGenerator::Generate(NameSpan value) {
  GenerateObject([&]() {
    GenerateObjectMember("filename", value.filename, Position::kFirst);
    GenerateObjectMember("line", (uint32_t)value.position.line);
    GenerateObjectMember("column", (uint32_t)value.position.column);
    GenerateObjectMember("length", (uint32_t)value.length);
  });
}

void JSONGenerator::Generate(const flat::ConstantValue& value) {
  switch (value.kind) {
    case flat::ConstantValue::Kind::kUint8:
    case flat::ConstantValue::Kind::kZxUchar: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint8_t>&>(value);
      EmitNumeric(static_cast<uint64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint16: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint16_t>&>(value);
      EmitNumeric(static_cast<uint16_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint32: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value);
      EmitNumeric(static_cast<uint32_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint64:
    case flat::ConstantValue::Kind::kZxUsize:
    case flat::ConstantValue::Kind::kZxUintptr: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint64_t>&>(value);
      EmitNumeric(static_cast<uint64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt8: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int8_t>&>(value);
      EmitNumeric(static_cast<int64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt16: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int16_t>&>(value);
      EmitNumeric(static_cast<int16_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt32: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int32_t>&>(value);
      EmitNumeric(static_cast<int32_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt64: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int64_t>&>(value);
      EmitNumeric(static_cast<int64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kFloat32: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<float>&>(value);
      EmitNumeric(static_cast<float>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kFloat64: {
      auto& numeric_constant = reinterpret_cast<const flat::NumericConstantValue<double>&>(value);
      EmitNumeric(static_cast<double>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kBool: {
      auto& bool_constant = reinterpret_cast<const flat::BoolConstantValue&>(value);
      EmitBoolean(static_cast<bool>(bool_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kDocComment: {
      auto& doc_comment_constant = reinterpret_cast<const flat::DocCommentConstantValue&>(value);
      EmitString(doc_comment_constant.MakeContents());
      break;
    }
    case flat::ConstantValue::Kind::kString: {
      auto& string_constant = reinterpret_cast<const flat::StringConstantValue&>(value);
      EmitLiteral(string_constant.value);
      break;
    }
  }  // switch
}

void JSONGenerator::Generate(types::HandleSubtype value) { EmitString(NameHandleSubtype(value)); }

void JSONGenerator::Generate(types::Nullability value) {
  switch (value) {
    case types::Nullability::kNullable:
      EmitBoolean(true);
      break;
    case types::Nullability::kNonnullable:
      EmitBoolean(false);
      break;
  }
}

void JSONGenerator::Generate(types::Strictness value) {
  EmitBoolean(value == types::Strictness::kStrict);
}

void JSONGenerator::Generate(types::Openness value) {
  switch (value) {
    case types::Openness::kOpen:
      EmitString("open");
      break;
    case types::Openness::kAjar:
      EmitString("ajar");
      break;
    case types::Openness::kClosed:
      EmitString("closed");
      break;
  }
}

void JSONGenerator::Generate(const raw::Identifier& value) { EmitString(value.span().data()); }

void JSONGenerator::Generate(const flat::LiteralConstant& value) {
  GenerateObject([&]() {
    GenerateObjectMember("kind", NameRawLiteralKind(value.literal->kind), Position::kFirst);
    GenerateObjectMember("value", value.Value());
    GenerateObjectMember("expression", value.literal->span().data());
  });
}

void JSONGenerator::Generate(const flat::Constant& value) {
  GenerateObject([&]() {
    GenerateObjectMember("kind", NameFlatConstantKind(value.kind), Position::kFirst);
    GenerateObjectMember("value", value.Value());
    GenerateObjectMember("expression", value.span);
    switch (value.kind) {
      case flat::Constant::Kind::kIdentifier: {
        auto identifier = static_cast<const flat::IdentifierConstant*>(&value);
        GenerateObjectMember("identifier", identifier->reference.resolved().name());
        break;
      }
      case flat::Constant::Kind::kLiteral: {
        auto literal = static_cast<const flat::LiteralConstant*>(&value);
        GenerateObjectMember("literal", *literal);
        break;
      }
      case flat::Constant::Kind::kBinaryOperator: {
        // Avoid emitting a structure for binary operators in favor of "expression".
        break;
      }
    }
  });
}

void JSONGenerator::Generate(const flat::Type* value) {
  if (value->kind == flat::Type::Kind::kBox)
    return Generate(static_cast<const flat::BoxType*>(value)->boxed_type);

  GenerateObject([&]() {
    GenerateObjectMember("kind", NameFlatTypeKind(value), Position::kFirst);

    switch (value->kind) {
      case flat::Type::Kind::kBox:
        ZX_PANIC("should be caught above");
      case flat::Type::Kind::kVector: {
        // This code path should only be exercised if the type is "bytes." All
        // other handling of kVector is handled in GenerateParameterizedType.
        const auto* type = static_cast<const flat::VectorType*>(value);
        GenerateObjectMember("element_type", type->element_type);
        if (*type->element_count < flat::Size::Max())
          GenerateObjectMember("maybe_element_count", type->element_count->value);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kString: {
        const auto* type = static_cast<const flat::StringType*>(value);
        if (*type->max_size < flat::Size::Max())
          GenerateObjectMember("maybe_element_count", type->max_size->value);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kHandle: {
        const auto* type = static_cast<const flat::HandleType*>(value);
        GenerateObjectMember("obj_type", type->obj_type);
        GenerateObjectMember("subtype", type->subtype);
        GenerateObjectMember(
            "rights",
            static_cast<const flat::NumericConstantValue<uint32_t>*>(type->rights)->value);
        GenerateObjectMember("nullable", type->nullability);
        GenerateObjectMember("resource_identifier", NameFlatName(type->resource_decl->name));
        break;
      }
      case flat::Type::Kind::kPrimitive: {
        const auto* type = static_cast<const flat::PrimitiveType*>(value);
        GenerateObjectMember("subtype", type->name);
        break;
      }
      case flat::Type::Kind::kInternal: {
        const auto* type = static_cast<const flat::InternalType*>(value);
        switch (type->subtype) {
          case types::InternalSubtype::kTransportErr:
            GenerateObjectMember("subtype", std::string_view("transport_error"));
            break;
        }
        break;
      }
      case flat::Type::Kind::kIdentifier: {
        const auto* type = static_cast<const flat::IdentifierType*>(value);
        GenerateObjectMember("identifier", type->name);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      // We treat client_end the same as an IdentifierType of a protocol to avoid changing
      // the JSON IR.
      // TODO(fxbug.dev/70186): clean up client/server end representation in the IR
      case flat::Type::Kind::kTransportSide: {
        const auto* type = static_cast<const flat::TransportSideType*>(value);
        // This code path should only apply to client ends. The server end code
        // path is colocated with the parameterized types.
        ZX_ASSERT(type->end == flat::TransportSide::kClient);
        GenerateObjectMember("identifier", type->protocol_decl->name);
        GenerateObjectMember("nullable", type->nullability);
        GenerateObjectMember("protocol_transport", type->protocol_transport);
        break;
      }
      case flat::Type::Kind::kZxExperimentalPointer: {
        const auto* type = static_cast<const flat::ZxExperimentalPointerType*>(value);
        GenerateObjectMember("pointee_type", type->pointee_type);
        break;
      }
      case flat::Type::Kind::kArray:
      case flat::Type::Kind::kUntypedNumeric:
        ZX_PANIC("unexpected kind");
    }

    GenerateTypeShapes(*value);
  });
}

void JSONGenerator::Generate(const flat::AttributeArg& value) {
  GenerateObject([&]() {
    ZX_ASSERT_MSG(
        value.name.has_value(),
        "anonymous attribute argument names should always be inferred during compilation");
    GenerateObjectMember("name", value.name.value(), Position::kFirst);
    GenerateObjectMember("type", value.value->type->name);
    GenerateObjectMember("value", value.value);

    // TODO(fxbug.dev/7660): Be consistent in emitting location fields.
    const SourceSpan& span = value.span;
    if (span.valid())
      GenerateObjectMember("location", NameSpan(span));
  });
}

void JSONGenerator::Generate(const flat::Attribute& value) {
  GenerateObject([&]() {
    const auto& name = fidl::utils::to_lower_snake_case(std::string(value.name.data()));
    GenerateObjectMember("name", name, Position::kFirst);
    GenerateObjectMember("arguments", value.args);

    // TODO(fxbug.dev/7660): Be consistent in emitting location fields.
    const SourceSpan& span = value.span;
    if (span.valid())
      GenerateObjectMember("location", NameSpan(span));
  });
}

void JSONGenerator::Generate(const flat::AttributeList& value) { Generate(value.attributes); }

void JSONGenerator::Generate(const raw::Ordinal64& value) { EmitNumeric(value.value); }

void JSONGenerator::GenerateDeclName(const flat::Name& name) {
  GenerateObjectMember("name", name, Position::kFirst);
  if (auto n = name.as_anonymous()) {
    GenerateObjectMember("naming_context", n->context->Context());
  } else {
    std::vector<std::string> ctx = {std::string(name.decl_name())};
    GenerateObjectMember("naming_context", ctx);
  }
}

void JSONGenerator::Generate(const flat::Name& name) {
  // TODO(fxbug.dev/92422): NameFlatName omits the library name for builtins,
  // since we want error messages to say "uint32" not "fidl/uint32". However,
  // builtins MAX and HEAD can end up in the JSON IR as identifier constants,
  // and to satisfy the schema we must produce a proper compound identifier
  // (with a library name). We should solve this in a cleaner way.
  if (name.is_intrinsic() && name.decl_name() == "MAX") {
    Generate(std::string_view("fidl/MAX"));
  } else if (name.is_intrinsic() && name.decl_name() == "HEAD") {
    Generate(std::string_view("fidl/HEAD"));
  } else {
    Generate(NameFlatName(name));
  }
}

void JSONGenerator::Generate(const flat::Bits& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.subtype_ctor.get());
    // TODO(fxbug.dev/7660): When all numbers are wrapped as string, we can simply
    // call GenerateObjectMember directly.
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("mask");
    EmitNumeric(value.mask, kAsString);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness);
  });
}

void JSONGenerator::Generate(const flat::Bits::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("value", value.value);
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Const& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.type_ctor.get());
    GenerateObjectMember("value", value.value);
  });
}

void JSONGenerator::Generate(const flat::Enum& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    // TODO(fxbug.dev/7660): Due to legacy reasons, the 'type' of enums is actually
    // the primitive subtype, and therefore cannot use
    // GenerateTypeAndFromTypeAlias here.
    GenerateObjectMember("type", value.type->name);
    GenerateExperimentalMaybeFromTypeAlias(value.subtype_ctor->resolved_params);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness);
    if (value.strictness == types::Strictness::kFlexible) {
      if (value.unknown_value_signed) {
        GenerateObjectMember("maybe_unknown_value", value.unknown_value_signed.value());
      } else {
        GenerateObjectMember("maybe_unknown_value", value.unknown_value_unsigned.value());
      }
    }
  });
}

void JSONGenerator::Generate(const flat::Enum::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("value", value.value);
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Protocol& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions))
      GenerateObjectMember("openness", value.openness);
    GenerateObjectMember("composed_protocols", value.composed_protocols);
    GenerateObjectMember("methods", value.all_methods);
  });
}

void JSONGenerator::Generate(const flat::Protocol::ComposedProtocol& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.reference.resolved().name(), Position::kFirst);
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("location", NameSpan(value.reference.span()));
  });
}

void JSONGenerator::Generate(const flat::Protocol::MethodWithInfo& method_with_info) {
  ZX_ASSERT(method_with_info.method != nullptr);
  const auto& value = *method_with_info.method;
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", value.generated_ordinal64, Position::kFirst);
    GenerateObjectMember("name", value.name);
    if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions))
      GenerateObjectMember("strict", value.strictness);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("has_request", value.has_request);
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.maybe_request) {
      GenerateTypeAndFromTypeAlias(TypeKind::kRequestPayload, value.maybe_request.get(),
                                   Position::kSubsequent);
    }
    GenerateObjectMember("has_response", value.has_response);
    if (value.maybe_response) {
      GenerateTypeAndFromTypeAlias(TypeKind::kResponsePayload, value.maybe_response.get(),
                                   Position::kSubsequent);
    }
    GenerateObjectMember("is_composed", method_with_info.is_composed);
    GenerateObjectMember("has_error", value.has_error);
    if (value.HasResultUnion()) {
      auto response_id = static_cast<const flat::IdentifierType*>(value.maybe_response->type);
      auto response_struct = static_cast<const flat::Struct*>(response_id->type_decl);
      const auto* result_union_type = response_struct->members[0].type_ctor->type;
      const auto* result_union = static_cast<const flat::Union*>(
          static_cast<const flat::IdentifierType*>(result_union_type)->type_decl);
      GenerateObjectMember("maybe_response_result_type", result_union_type);
      GenerateObjectMember("maybe_response_success_type",
                           result_union->members[0].maybe_used->type_ctor->type);
      if (value.has_error) {
        GenerateObjectMember("maybe_response_err_type",
                             result_union->members[1].maybe_used->type_ctor->type);
      }
    }
  });
}

void JSONGenerator::GenerateTypeAndFromTypeAlias(const flat::TypeConstructor* value,
                                                 Position position) {
  GenerateTypeAndFromTypeAlias(TypeKind::kConcrete, value, position);
}

bool ShouldExposeTypeAliasOfParametrizedType(const flat::Type& type) {
  bool is_server_end = false;
  if (type.kind == flat::Type::Kind::kTransportSide) {
    const auto* transport_side = static_cast<const flat::TransportSideType*>(&type);
    is_server_end = transport_side->end == flat::TransportSide::kServer;
  }
  return type.kind == flat::Type::Kind::kArray || type.kind == flat::Type::Kind::kVector ||
         is_server_end;
}

void JSONGenerator::GenerateTypeAndFromTypeAlias(TypeKind parent_type_kind,
                                                 const flat::TypeConstructor* value,
                                                 Position position) {
  const auto* type = value->type;
  const auto& invocation = value->resolved_params;
  if (fidl::ShouldExposeTypeAliasOfParametrizedType(*type)) {
    if (invocation.from_type_alias) {
      GenerateParameterizedType(parent_type_kind, type,
                                invocation.from_type_alias->partial_type_ctor.get(), position);
    } else {
      GenerateParameterizedType(parent_type_kind, type, value, position);
    }
    GenerateExperimentalMaybeFromTypeAlias(invocation);
    return;
  }

  std::string key;
  switch (parent_type_kind) {
    case kConcrete: {
      key = "type";
      break;
    }
    case kParameterized: {
      key = "element_type";
      break;
    }
    case kRequestPayload: {
      key = "maybe_request_payload";
      break;
    }
    case kResponsePayload: {
      key = "maybe_response_payload";
      break;
    }
  }

  GenerateObjectMember(key, type, position);
  GenerateExperimentalMaybeFromTypeAlias(invocation);
}

void JSONGenerator::GenerateExperimentalMaybeFromTypeAlias(
    const flat::LayoutInvocation& invocation) {
  if (invocation.from_type_alias)
    GenerateObjectMember("experimental_maybe_from_type_alias", invocation);
}

void JSONGenerator::GenerateParameterizedType(TypeKind parent_type_kind, const flat::Type* type,
                                              const flat::TypeConstructor* type_ctor,
                                              Position position) {
  const auto& invocation = type_ctor->resolved_params;
  std::string key = parent_type_kind == TypeKind::kConcrete ? "type" : "element_type";

  // Special case: type "bytes" is a builtin alias, so it will have no
  // user-specified arg type.
  if (type->kind == flat::Type::Kind::kVector && invocation.element_type_raw == nullptr) {
    GenerateObjectMember(key, type, position);
    return;
  }

  GenerateObjectPunctuation(position);
  EmitObjectKey(key);
  GenerateObject([&]() {
    GenerateObjectMember("kind", NameFlatTypeKind(type), Position::kFirst);

    switch (type->kind) {
      case flat::Type::Kind::kArray: {
        const auto* array_type = static_cast<const flat::ArrayType*>(type);
        GenerateTypeAndFromTypeAlias(TypeKind::kParameterized, invocation.element_type_raw);
        GenerateObjectMember("element_count", array_type->element_count->value);
        break;
      }
      case flat::Type::Kind::kVector: {
        const auto* vector_type = static_cast<const flat::VectorType*>(type);
        GenerateTypeAndFromTypeAlias(TypeKind::kParameterized, invocation.element_type_raw);
        if (*vector_type->element_count < flat::Size::Max())
          GenerateObjectMember("maybe_element_count", vector_type->element_count->value);
        GenerateObjectMember("nullable", vector_type->nullability);
        break;
      }
      case flat::Type::Kind::kTransportSide: {
        const auto* server_end = static_cast<const flat::TransportSideType*>(type);
        // This code path should only apply to server ends. The client end code
        // path is colocated with the identifier type code for protocols.
        ZX_ASSERT(server_end->end == flat::TransportSide::kServer);
        GenerateObjectMember("subtype", server_end->protocol_decl->name);
        // We don't need to call GenerateExperimentalMaybeFromTypeAlias here like we
        // do above because we're guaranteed that the protocol constraint didn't come
        // from a type alias: in the new syntax, protocols aren't types, and therefore
        // `alias Foo = MyProtocol;` is not allowed.
        GenerateObjectMember("nullable", server_end->nullability);
        GenerateObjectMember("protocol_transport", server_end->protocol_transport);
        break;
      }
      case flat::Type::Kind::kZxExperimentalPointer: {
        GenerateTypeAndFromTypeAlias(TypeKind::kParameterized, invocation.element_type_raw);
        break;
      }
      case flat::Type::Kind::kIdentifier:
      case flat::Type::Kind::kString:
      case flat::Type::Kind::kPrimitive:
      case flat::Type::Kind::kBox:
      case flat::Type::Kind::kHandle:
      case flat::Type::Kind::kUntypedNumeric:
        ZX_PANIC("unexpected kind");
      case flat::Type::Kind::kInternal: {
        switch (static_cast<const flat::InternalType*>(type)->subtype) {
          case types::InternalSubtype::kTransportErr:
            ZX_PANIC("unexpected kind");
        }
      }
    }
    GenerateTypeShapes(*type);
  });
}

void JSONGenerator::Generate(const flat::Resource::Property& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateTypeAndFromTypeAlias(value.type_ctor.get());
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Resource& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.subtype_ctor.get());
    GenerateObjectMember("properties", value.properties);
  });
}

void JSONGenerator::Generate(const flat::Service& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
  });
}

void JSONGenerator::Generate(const flat::Service::Member& value) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(value.type_ctor.get(), Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Struct& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));

    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Struct::Member& value) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(value.type_ctor.get(), Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.maybe_default_value)
      GenerateObjectMember("maybe_default_value", value.maybe_default_value);
    GenerateFieldShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Table& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Table::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", *value.ordinal, Position::kFirst);
    if (value.maybe_used) {
      ZX_ASSERT(!value.span);
      GenerateObjectMember("reserved", false);
      GenerateTypeAndFromTypeAlias(value.maybe_used->type_ctor.get());
      GenerateObjectMember("name", value.maybe_used->name);
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
      // TODO(fxbug.dev/7932): Support defaults on tables.
    } else {
      ZX_ASSERT(value.span);
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
    }

    if (!value.attributes->Empty()) {
      GenerateObjectMember("maybe_attributes", value.attributes);
    }
  });
}

void JSONGenerator::Generate(const TypeShape& type_shape) {
  GenerateObject([&]() {
    GenerateObjectMember("inline_size", type_shape.inline_size, Position::kFirst);
    GenerateObjectMember("alignment", type_shape.alignment);
    GenerateObjectMember("depth", type_shape.depth);
    GenerateObjectMember("max_handles", type_shape.max_handles);
    GenerateObjectMember("max_out_of_line", type_shape.max_out_of_line);
    GenerateObjectMember("has_padding", type_shape.has_padding);
    GenerateObjectMember("has_envelope", type_shape.has_envelope);
    GenerateObjectMember("has_flexible_envelope", type_shape.has_flexible_envelope);
  });
}

void JSONGenerator::Generate(const FieldShape& field_shape) {
  GenerateObject([&]() {
    GenerateObjectMember("offset", field_shape.offset, Position::kFirst);
    GenerateObjectMember("padding", field_shape.padding);
  });
}

void JSONGenerator::Generate(const flat::Union& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", value.ordinal, Position::kFirst);
    if (value.maybe_used) {
      ZX_ASSERT(!value.span);
      GenerateObjectMember("reserved", false);
      GenerateObjectMember("name", value.maybe_used->name);
      GenerateTypeAndFromTypeAlias(value.maybe_used->type_ctor.get());
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
    } else {
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
    }

    if (!value.attributes->Empty()) {
      GenerateObjectMember("maybe_attributes", value.attributes);
    }
  });
}

void JSONGenerator::Generate(const flat::LayoutInvocation& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.from_type_alias->name, Position::kFirst);
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("args");

    // In preparation of template support, it is better to expose a
    // heterogeneous argument list to backends, rather than the currently
    // limited internal view.
    EmitArrayBegin();
    if (value.element_type_resolved) {
      Indent();
      EmitNewlineWithIndent();
      Generate(value.element_type_raw->layout.resolved().name());
      Outdent();
      EmitNewlineWithIndent();
    }
    EmitArrayEnd();

    GenerateObjectMember("nullable", value.nullability);

    if (value.size_raw)
      GenerateObjectMember("maybe_size", *value.size_raw);
  });
}

void JSONGenerator::Generate(const flat::TypeConstructor& value) {
  GenerateObject([&]() {
    const auto* type = value.type;
    bool is_box = false;
    // TODO(fxbug.dev/70186): We need to coerce client/server
    // ends into the same representation as P, request<P>; and box<S> into S?
    // For box, we just need to access the inner IdentifierType and the rest
    // mostly works (except for the correct value for nullability)
    if (type && type->kind == flat::Type::Kind::kBox) {
      type = static_cast<const flat::BoxType*>(type)->boxed_type;
      is_box = true;
    }
    const flat::TransportSideType* server_end = nullptr;
    if (type && type->kind == flat::Type::Kind::kTransportSide) {
      const auto* end_type = static_cast<const flat::TransportSideType*>(type);
      if (end_type->end == flat::TransportSide::kClient) {
        // for client ends, the partial_type_ctor name should be the protocol name
        // (since client_end:P is P in the old syntax)
        GenerateObjectMember("name", end_type->protocol_decl->name, Position::kFirst);
      } else {
        // for server ends, the partial_type_ctor name is just "request" (since
        // server_end:P is request<P> in the old syntax), and we also need to
        // emit the protocol "arg" below
        GenerateObjectMember("name", flat::Name::CreateIntrinsic(nullptr, "request"),
                             Position::kFirst);
        server_end = end_type;
      }
    } else {
      GenerateObjectMember("name", value.type ? value.type->name : value.layout.resolved().name(),
                           Position::kFirst);
    }
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("args");
    const auto& invocation = value.resolved_params;

    // In preparation of template support, it is better to expose a
    // heterogeneous argument list to backends, rather than the currently
    // limited internal view.
    EmitArrayBegin();
    if (server_end || is_box || invocation.element_type_resolved) {
      Indent();
      EmitNewlineWithIndent();
      if (server_end) {
        // TODO(fxbug.dev/70186): Because the JSON IR still uses request<P>
        // instead of server_end:P, we have to hardcode the P argument here.
        GenerateObject([&]() {
          GenerateObjectMember("name", server_end->protocol_decl->name, Position::kFirst);
          GenerateObjectPunctuation(Position::kSubsequent);
          EmitObjectKey("args");
          EmitArrayBegin();
          EmitArrayEnd();
          GenerateObjectMember("nullable", types::Nullability::kNonnullable);
        });
      } else if (is_box) {
        Generate(*invocation.boxed_type_raw);
      } else {
        Generate(*invocation.element_type_raw);
      }
      Outdent();
      EmitNewlineWithIndent();
    }
    EmitArrayEnd();

    if (value.type && value.type->kind == flat::Type::Kind::kBox) {
      // invocation.nullability will always be non nullable, because users can't
      // specify optional on box. however, we need to output nullable in this case
      // in order to match the behavior for Struct?
      GenerateObjectMember("nullable", types::Nullability::kNullable);
    } else {
      GenerateObjectMember("nullable", invocation.nullability);
    }

    if (invocation.size_raw)
      GenerateObjectMember("maybe_size", *invocation.size_raw);
    if (invocation.rights_raw)
      GenerateObjectMember("handle_rights", *invocation.rights_raw);
  });
}

void JSONGenerator::Generate(const flat::TypeAlias& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("partial_type_ctor", value.partial_type_ctor);
  });
}

void JSONGenerator::Generate(const flat::NewType& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (!value.attributes->Empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.type_ctor.get());
  });
}

void JSONGenerator::Generate(const flat::Compilation::Dependency& dependency) {
  GenerateObject([&]() {
    auto library_name = flat::LibraryName(dependency.library->name, ".");
    GenerateObjectMember("name", library_name, Position::kFirst);
    GenerateExternalDeclarationsMember(dependency.declarations);
  });
}

void JSONGenerator::GenerateTypeShapes(const flat::Object& object) {
  GenerateObjectMember("type_shape_v1", TypeShape(object, WireFormat::kV1NoEe));
  GenerateObjectMember("type_shape_v2", TypeShape(object, WireFormat::kV2));
}

void JSONGenerator::GenerateFieldShapes(const flat::Struct::Member& struct_member) {
  auto v1 = FieldShape(struct_member, WireFormat::kV1NoEe);
  GenerateObjectMember("field_shape_v1", v1);
  auto v2 = FieldShape(struct_member, WireFormat::kV2);
  GenerateObjectMember("field_shape_v2", v2);
}

void JSONGenerator::GenerateDeclarationsEntry(int count, const flat::Name& name,
                                              std::string_view decl_kind) {
  if (count == 0) {
    Indent();
    EmitNewlineWithIndent();
  } else {
    EmitObjectSeparator();
  }
  EmitObjectKey(NameFlatName(name));
  EmitString(decl_kind);
}

void JSONGenerator::GenerateDeclarationsMember(const flat::Compilation::Declarations& declarations,
                                               Position position) {
  GenerateObjectPunctuation(position);
  EmitObjectKey("declarations");
  GenerateObject([&]() {
    int count = 0;
    for (const auto& decl : declarations.bits)
      GenerateDeclarationsEntry(count++, decl->name, "bits");

    for (const auto& decl : declarations.consts)
      GenerateDeclarationsEntry(count++, decl->name, "const");

    for (const auto& decl : declarations.enums)
      GenerateDeclarationsEntry(count++, decl->name, "enum");

    for (const auto& decl : declarations.resources)
      GenerateDeclarationsEntry(count++, decl->name, "experimental_resource");

    for (const auto& decl : declarations.protocols)
      GenerateDeclarationsEntry(count++, decl->name, "protocol");

    for (const auto& decl : declarations.services)
      GenerateDeclarationsEntry(count++, decl->name, "service");

    for (const auto& decl : declarations.structs)
      GenerateDeclarationsEntry(count++, decl->name, "struct");

    for (const auto& decl : declarations.tables)
      GenerateDeclarationsEntry(count++, decl->name, "table");

    for (const auto& decl : declarations.unions)
      GenerateDeclarationsEntry(count++, decl->name, "union");

    for (const auto& decl : declarations.type_aliases)
      GenerateDeclarationsEntry(count++, decl->name, "type_alias");

    for (const auto& decl : declarations.new_types)
      GenerateDeclarationsEntry(count++, decl->name, "new_type");
  });
}

void JSONGenerator::GenerateExternalDeclarationsEntry(
    int count, const flat::Name& name, std::string_view decl_kind,
    std::optional<types::Resourceness> maybe_resourceness) {
  if (count == 0) {
    Indent();
    EmitNewlineWithIndent();
  } else {
    EmitObjectSeparator();
  }
  EmitObjectKey(NameFlatName(name));
  GenerateObject([&]() {
    GenerateObjectMember("kind", decl_kind, Position::kFirst);
    if (maybe_resourceness) {
      GenerateObjectMember("resource", *maybe_resourceness == types::Resourceness::kResource);
    }
  });
}

void JSONGenerator::GenerateExternalDeclarationsMember(
    const flat::Compilation::Declarations& declarations, Position position) {
  GenerateObjectPunctuation(position);
  EmitObjectKey("declarations");
  GenerateObject([&]() {
    int count = 0;
    for (const auto& decl : declarations.bits)
      GenerateExternalDeclarationsEntry(count++, decl->name, "bits", std::nullopt);

    for (const auto& decl : declarations.consts)
      GenerateExternalDeclarationsEntry(count++, decl->name, "const", std::nullopt);

    for (const auto& decl : declarations.enums)
      GenerateExternalDeclarationsEntry(count++, decl->name, "enum", std::nullopt);

    for (const auto& decl : declarations.resources)
      GenerateExternalDeclarationsEntry(count++, decl->name, "experimental_resource", std::nullopt);

    for (const auto& decl : declarations.protocols)
      GenerateExternalDeclarationsEntry(count++, decl->name, "protocol", std::nullopt);

    for (const auto& decl : declarations.services)
      GenerateExternalDeclarationsEntry(count++, decl->name, "service", std::nullopt);

    for (const auto& decl : declarations.structs)
      GenerateExternalDeclarationsEntry(count++, decl->name, "struct", decl->resourceness);

    for (const auto& decl : declarations.tables)
      GenerateExternalDeclarationsEntry(count++, decl->name, "table", decl->resourceness);

    for (const auto& decl : declarations.unions)
      GenerateExternalDeclarationsEntry(count++, decl->name, "union", decl->resourceness);

    for (const auto& decl : declarations.type_aliases)
      GenerateExternalDeclarationsEntry(count++, decl->name, "type_alias", std::nullopt);

    for (const auto& decl : declarations.new_types)
      GenerateExternalDeclarationsEntry(count++, decl->name, "new_type", std::nullopt);
  });
}

std::ostringstream JSONGenerator::Produce() {
  ResetIndentLevel();
  GenerateObject([&]() {
    GenerateObjectMember("name", flat::LibraryName(compilation_->library_name, "."),
                         Position::kFirst);

    if (!compilation_->library_attributes->Empty()) {
      GenerateObjectMember("maybe_attributes", compilation_->library_attributes);
    }

    std::vector<const std::string_view> active_experiments;
    experimental_flags_.ForEach(
        [&](const std::string_view name, ExperimentalFlags::Flag flag, bool enabled) {
          if (enabled) {
            active_experiments.push_back(name);
          }
        });
    GenerateObjectMember("experiments", active_experiments);

    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("library_dependencies");
    GenerateArray(compilation_->direct_and_composed_dependencies);

    GenerateObjectMember("bits_declarations", compilation_->declarations.bits);
    GenerateObjectMember("const_declarations", compilation_->declarations.consts);
    GenerateObjectMember("enum_declarations", compilation_->declarations.enums);
    GenerateObjectMember("experimental_resource_declarations",
                         compilation_->declarations.resources);
    GenerateObjectMember("protocol_declarations", compilation_->declarations.protocols);
    GenerateObjectMember("service_declarations", compilation_->declarations.services);
    GenerateObjectMember("struct_declarations", compilation_->declarations.structs);
    GenerateObjectMember("external_struct_declarations", compilation_->external_structs);
    GenerateObjectMember("table_declarations", compilation_->declarations.tables);
    GenerateObjectMember("union_declarations", compilation_->declarations.unions);
    GenerateObjectMember("type_alias_declarations", compilation_->declarations.type_aliases);
    GenerateObjectMember("new_type_declarations", compilation_->declarations.new_types);

    std::vector<std::string> declaration_order;
    for (const auto decl : compilation_->declaration_order) {
      declaration_order.push_back(NameFlatName(decl->name));
    }
    GenerateObjectMember("declaration_order", declaration_order);

    GenerateDeclarationsMember(compilation_->declarations);
  });
  GenerateEOF();

  return std::move(json_file_);
}

}  // namespace fidl
