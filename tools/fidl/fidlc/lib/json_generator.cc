// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/json_generator.h"

#include "fidl/diagnostic_types.h"
#include "fidl/flat/name.h"
#include "fidl/flat/types.h"
#include "fidl/flat_ast.h"
#include "fidl/names.h"
#include "fidl/types.h"

namespace fidl {

void JSONGenerator::Generate(const flat::Decl* decl) { Generate(decl->name); }

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
    case flat::ConstantValue::Kind::kUint8: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint8_t>&>(value);
      EmitNumeric(static_cast<uint64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint16: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint16_t>&>(value);
      EmitNumeric(static_cast<uint16_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint32: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint32_t>&>(value);
      EmitNumeric(static_cast<uint32_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kUint64: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<uint64_t>&>(value);
      EmitNumeric(static_cast<uint64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt8: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int8_t>&>(value);
      EmitNumeric(static_cast<int64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt16: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int16_t>&>(value);
      EmitNumeric(static_cast<int16_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt32: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int32_t>&>(value);
      EmitNumeric(static_cast<int32_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kInt64: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<int64_t>&>(value);
      EmitNumeric(static_cast<int64_t>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kFloat32: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<float>&>(value);
      EmitNumeric(static_cast<float>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kFloat64: {
      auto numeric_constant = reinterpret_cast<const flat::NumericConstantValue<double>&>(value);
      EmitNumeric(static_cast<double>(numeric_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kBool: {
      auto bool_constant = reinterpret_cast<const flat::BoolConstantValue&>(value);
      EmitBoolean(static_cast<bool>(bool_constant), kAsString);
      break;
    }
    case flat::ConstantValue::Kind::kDocComment: {
      auto doc_comment_constant = reinterpret_cast<const flat::DocCommentConstantValue&>(value);
      EmitString(doc_comment_constant.MakeContents());
      break;
    }
    case flat::ConstantValue::Kind::kString: {
      auto string_constant = reinterpret_cast<const flat::StringConstantValue&>(value);
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
        auto type = static_cast<const flat::IdentifierConstant*>(&value);
        GenerateObjectMember("identifier", type->name);
        break;
      }
      case flat::Constant::Kind::kLiteral: {
        auto& type = static_cast<const flat::LiteralConstant&>(value);
        GenerateObjectMember("literal", type);
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
        assert(false && "should be caught above");
        __builtin_unreachable();
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
        break;
      }
      case flat::Type::Kind::kPrimitive: {
        const auto* type = static_cast<const flat::PrimitiveType*>(value);
        GenerateObjectMember("subtype", type->name);
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
        assert(type->end == flat::TransportSide::kClient);
        GenerateObjectMember("identifier", type->protocol_decl->name);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kArray: {
        assert(false &&
               "expected non-parameterized type (neither array<T>, vector<T>, nor request<P>)");
      }
    }
  });
}

void JSONGenerator::Generate(const flat::AttributeArg& value) {
  GenerateObject([&]() {
    assert(value.name.has_value() &&
           "anonymous attribute argument names should always be inferred during compilation");
    GenerateObjectMember("name", value.name.value(), Position::kFirst);
    GenerateObjectMember("value", value.value);

    // TODO(fxbug.dev/7660): Be consistent in emitting location fields.
    const SourceSpan& span = value.span();
    if (span.valid())
      GenerateObjectMember("location", NameSpan(span));
  });
}

void JSONGenerator::Generate(const flat::Attribute& value) {
  GenerateObject([&]() {
    const auto& name = fidl::utils::to_lower_snake_case(value.name);
    GenerateObjectMember("name", name, Position::kFirst);
    GenerateObjectMember("arguments", value.args);

    // TODO(fxbug.dev/7660): Be consistent in emitting location fields.
    const SourceSpan& span = value.span();
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

void JSONGenerator::Generate(const flat::Name& value) {
  // These look like (when there is a library)
  //     { "LIB.LIB.LIB", "ID" }
  // or (when there is not)
  //     { "ID" }
  Generate(NameFlatName(value));
}

void JSONGenerator::Generate(const flat::Bits& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    // TODO(fxbug.dev/79094): refactor this to only have a single null state.
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.subtype_ctor.get());
    // TODO(fxbug.dev/7660): When all numbers are wrapped as string, we can simply
    // call GenerateObjectMember directly.
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("mask");
    EmitNumeric(value.mask, kAsString);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness == types::Strictness::kStrict);
  });
}

void JSONGenerator::Generate(const flat::Bits::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("value", value.value);
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Const& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.type_ctor.get());
    GenerateObjectMember("value", value.value);
  });
}

void JSONGenerator::Generate(const flat::Enum& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    // TODO(fxbug.dev/7660): Due to legacy reasons, the 'type' of enums is actually
    // the primitive subtype, and therefore cannot use
    // GenerateTypeAndFromTypeAlias here.
    GenerateObjectMember("type", value.type->name);
    GenerateExperimentalMaybeFromTypeAlias(value.subtype_ctor->resolved_params);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness == types::Strictness::kStrict);
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
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Protocol& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("composed_protocols", value.composed_protocols);
    GenerateObjectMember("methods", value.all_methods);
  });
}

void JSONGenerator::Generate(const flat::Protocol::ComposedProtocol& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("location", NameSpan(value.name));
  });
}

void JSONGenerator::Generate(const flat::Protocol::MethodWithInfo& method_with_info) {
  assert(method_with_info.method != nullptr);
  const auto& value = *method_with_info.method;
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", value.generated_ordinal64, Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("has_request", value.has_request);
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.has_request) {
      GenerateRequest("maybe_request", value.maybe_request_payload);
    }
    GenerateObjectMember("has_response", value.has_response);
    if (value.has_response) {
      GenerateRequest("maybe_response", value.maybe_response_payload);
    }
    GenerateObjectMember("is_composed", method_with_info.is_composed);
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

  std::string key = parent_type_kind == TypeKind::kConcrete ? "type" : "element_type";
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
        assert(server_end->end == flat::TransportSide::kServer);
        GenerateObjectMember("subtype", server_end->protocol_decl->name);
        // We don't need to call GenerateExperimentalMaybeFromTypeAlias here like we
        // do above because we're guaranteed that the protocol constraint didn't come
        // from a type alias: in the new syntax, protocols aren't types, and therefore
        // `alias Foo = MyProtocol;` is not allowed.
        GenerateObjectMember("nullable", server_end->nullability);
        break;
      }
      case flat::Type::Kind::kIdentifier:
      case flat::Type::Kind::kString:
      case flat::Type::Kind::kPrimitive:
      case flat::Type::Kind::kBox:
      case flat::Type::Kind::kHandle: {
        assert(false && "expected parameterized type (either array<T>, vector<T>, or request<P>)");
      }
    }
  });
}

void JSONGenerator::GenerateRequest(const std::string& prefix, const flat::Struct* value) {
  // TODO(fxbug.dev/76316): remove this assert once this generator is able to
  //  properly handle empty structs as payloads.
  assert((!value || (value && !value->members.empty())) && "cannot process empty message payloads");

  // Temporarily hardcode the generation of request/response struct members to use the old
  // wire format, in order to maintain compatibility during the transition for fxbug.dev/7704.
  // This block of code is copied from JsonWriter::GenerateArray (with the difference
  // noted below), and will be removed once backends are updated to use anonymous structs.
  GenerateObjectPunctuation(Position::kSubsequent);
  EmitObjectKey(prefix);
  EmitArrayBegin();
  if (value) {
    if (value->members.begin() != value->members.end()) {
      Indent();
      EmitNewlineWithIndent();
    }
    for (auto it = value->members.begin(); it != value->members.end(); ++it) {
      if (it != value->members.begin())
        EmitArraySeparator();
      // call Generate with is_request_response = true on each struct member
      Generate(*it, true);
    }
    if (value->members.begin() != value->members.end()) {
      Outdent();
      EmitNewlineWithIndent();
    }
  }
  EmitArrayEnd();

  if (value) {
    if (!value->members.empty()) {
      GenerateObjectMember(prefix + "_payload", value->name);
    }
  }
  GenerateTypeShapes(prefix, value);
}

void JSONGenerator::Generate(const flat::Resource::Property& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateTypeAndFromTypeAlias(value.type_ctor.get());
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Resource& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(value.subtype_ctor.get());
    GenerateObjectMember("properties", value.properties);
  });
}

void JSONGenerator::Generate(const flat::Service& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
  });
}

void JSONGenerator::Generate(const flat::Service::Member& value) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(value.type_ctor.get(), Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Struct& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("is_request_or_response", value.is_request_or_response);
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Struct* value) { Generate(*value); }

void JSONGenerator::Generate(const flat::Struct::Member& value, bool is_request_or_response) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(value.type_ctor.get(), Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.maybe_default_value)
      GenerateObjectMember("maybe_default_value", value.maybe_default_value);
    GenerateFieldShapes(value, is_request_or_response);
  });
}

void JSONGenerator::Generate(const flat::Table& value) {
  GenerateObject([&]() {
    GenerateDeclName(value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness == types::Strictness::kStrict);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Table::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", *value.ordinal, Position::kFirst);
    if (value.maybe_used) {
      assert(!value.span);
      GenerateObjectMember("reserved", false);
      GenerateTypeAndFromTypeAlias(value.maybe_used->type_ctor.get());
      GenerateObjectMember("name", value.maybe_used->name);
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
      // TODO(fxbug.dev/7932): Support defaults on tables.
    } else {
      assert(value.span);
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
    }

    if (value.attributes && !value.attributes->attributes.empty()) {
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
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("strict", value.strictness == types::Strictness::kStrict);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Union::Member& value) {
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", value.ordinal, Position::kFirst);
    if (value.maybe_used) {
      assert(!value.span);
      GenerateObjectMember("reserved", false);
      GenerateObjectMember("name", value.maybe_used->name);
      GenerateTypeAndFromTypeAlias(value.maybe_used->type_ctor.get());
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
    } else {
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
    }

    if (value.attributes && !value.attributes->attributes.empty()) {
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
      Generate(value.element_type_raw->name);
      Outdent();
      EmitNewlineWithIndent();
    }
    EmitArrayEnd();

    GenerateObjectMember("nullable", value.nullability);

    if (value.size_resolved)
      GenerateObjectMember("maybe_size", *value.size_resolved);
  });
}

void JSONGenerator::Generate(const flat::TypeConstructor& value) {
  GenerateObject([&]() {
    const auto* type = value.type;
    // TODO(fxbug.dev/70186): We need to coerce client/server
    // ends into the same representation as P, request<P>; and box<S> into S?
    // For box, we just need to access the inner IdentifierType and the rest
    // mostly works (except for the correct value for nullability)
    if (type && type->kind == flat::Type::Kind::kBox)
      type = static_cast<const flat::BoxType*>(type)->boxed_type;
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
        GenerateObjectMember("name", flat::Name::CreateIntrinsic("request"), Position::kFirst);
        server_end = end_type;
      }
    } else {
      GenerateObjectMember("name", value.type ? value.type->name : value.name, Position::kFirst);
    }
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("args");
    const auto& invocation = value.resolved_params;

    // In preparation of template support, it is better to expose a
    // heterogeneous argument list to backends, rather than the currently
    // limited internal view.
    EmitArrayBegin();
    if (server_end || invocation.element_type_resolved) {
      Indent();
      EmitNewlineWithIndent();
      if (server_end) {
        // TODO(fxbug.dev/70186): We need to emit a "fake" arg for server
        // ends - create a TypeConstructor to simulate this argument
        std::vector<std::unique_ptr<flat::LayoutParameter>> no_params;
        std::vector<std::unique_ptr<flat::Constant>> no_constraints;
        flat::TypeConstructor type_ctor(server_end->protocol_decl->name,
                                        std::make_unique<flat::LayoutParameterList>(
                                            std::move(no_params), std::nullopt /* span */),
                                        std::make_unique<flat::TypeConstraints>(
                                            std::move(no_constraints), std::nullopt /* span */));
        Generate(type_ctor);
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
    if (value.attributes && !value.attributes->attributes.empty())
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("partial_type_ctor", value.partial_type_ctor);
  });
}

void JSONGenerator::Generate(const flat::Library* library) {
  GenerateObject([&]() {
    auto library_name = flat::LibraryName(library, ".");
    GenerateObjectMember("name", library_name, Position::kFirst);
    GenerateExternalDeclarationsMember(library);
  });
}

void JSONGenerator::GenerateTypeShapes(const flat::Object& object) {
  GenerateObjectMember("type_shape_v1", TypeShape(object, WireFormat::kV1Header));
  GenerateObjectMember("type_shape_v2", TypeShape(object, WireFormat::kV2Header));
}

void JSONGenerator::GenerateTypeShapes(std::string prefix, const flat::Struct* value) {
  assert((!value || (value && value->is_request_or_response)) &&
         "non-null value must be a request/response");
  if (prefix.size() > 0) {
    prefix.push_back('_');
  }

  auto typeshapeV1 = value ? TypeShape(value, WireFormat::kV1NoEe) : TypeShape::ForEmptyPayload();
  GenerateObjectMember(prefix + "type_shape_v1", typeshapeV1);
  auto typeshapeV2 = value ? TypeShape(value, WireFormat::kV2) : TypeShape::ForEmptyPayload();
  GenerateObjectMember(prefix + "type_shape_v2", typeshapeV2);
}

void JSONGenerator::GenerateFieldShapes(const flat::Struct::Member& struct_member,
                                        bool is_request_or_response) {
  // NOTE: while the transition for fxbug.dev/7024 is ongoing, we need to treat request/responses
  // specially as before, but this will be removed once the transition is complete
  const auto& v1 = is_request_or_response ? WireFormat::kV1NoEe : WireFormat::kV1Header;
  GenerateObjectMember("field_shape_v1", FieldShape(struct_member, v1));
  const auto& v2 = is_request_or_response ? WireFormat::kV2 : WireFormat::kV2Header;
  GenerateObjectMember("field_shape_v2", FieldShape(struct_member, v2));
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

void JSONGenerator::GenerateDeclarationsMember(const flat::Library* library, Position position) {
  GenerateObjectPunctuation(position);
  EmitObjectKey("declarations");
  GenerateObject([&]() {
    int count = 0;
    for (const auto& decl : library->bits_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "bits");

    for (const auto& decl : library->const_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "const");

    for (const auto& decl : library->enum_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "enum");

    for (const auto& decl : library->resource_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "experimental_resource");

    for (const auto& decl : library->protocol_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "interface");

    for (const auto& decl : library->service_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "service");

    for (const auto& decl : library->struct_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "struct");

    for (const auto& decl : library->table_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "table");

    for (const auto& decl : library->union_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "union");

    for (const auto& decl : library->type_alias_declarations_)
      GenerateDeclarationsEntry(count++, decl->name, "type_alias");
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

void JSONGenerator::GenerateExternalDeclarationsMember(const flat::Library* library,
                                                       Position position) {
  GenerateObjectPunctuation(position);
  EmitObjectKey("declarations");
  GenerateObject([&]() {
    int count = 0;
    for (const auto& decl : library->bits_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "bits", std::nullopt);

    for (const auto& decl : library->const_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "const", std::nullopt);

    for (const auto& decl : library->enum_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "enum", std::nullopt);

    for (const auto& decl : library->resource_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "experimental_resource", std::nullopt);

    for (const auto& decl : library->protocol_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "interface", std::nullopt);

    for (const auto& decl : library->service_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "service", std::nullopt);

    for (const auto& decl : library->struct_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "struct", decl->resourceness);

    for (const auto& decl : library->table_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "table", decl->resourceness);

    for (const auto& decl : library->union_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "union", decl->resourceness);

    for (const auto& decl : library->type_alias_declarations_)
      GenerateExternalDeclarationsEntry(count++, decl->name, "type_alias", std::nullopt);
  });
}

namespace {

// Return all structs that should be emitted in the JSON IR, which consists of
// two parts: all structs from this library (which includes all struct definitions
// and request/response payloads defined in this library), plus any request/response
// payloads defined in other libraries that are composed into a protocol in this
// library.
std::vector<const flat::Struct*> AllStructs(const flat::Library* library) {
  std::vector<const flat::Struct*> all_structs;

  for (const auto& struct_decl : library->struct_declarations_) {
    if (struct_decl->is_request_or_response && struct_decl->members.empty())
      continue;
    all_structs.push_back(struct_decl.get());
  }

  for (const auto& protocol : library->protocol_declarations_) {
    for (const auto method_with_info : protocol->all_methods) {
      // these are already included in the library's struct declarations
      if (!method_with_info.is_composed)
        continue;
      const auto& method = method_with_info.method;
      if (method->maybe_request_payload) {
        all_structs.push_back(method->maybe_request_payload);
      }
      if (method->maybe_response_payload) {
        all_structs.push_back(method->maybe_response_payload);
      }
    }
  }

  return all_structs;
}

}  // namespace

std::ostringstream JSONGenerator::Produce() {
  ResetIndentLevel();
  GenerateObject([&]() {
    GenerateObjectMember("version", std::string_view("0.0.1"), Position::kFirst);

    GenerateObjectMember("name", LibraryName(library_, "."));

    if (auto attributes = library_->GetAttributes(); attributes) {
      GenerateObjectMember("maybe_attributes", *attributes);
    }

    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("library_dependencies");
    GenerateArray(library_->DirectDependencies());

    GenerateObjectMember("bits_declarations", library_->bits_declarations_);
    GenerateObjectMember("const_declarations", library_->const_declarations_);
    GenerateObjectMember("enum_declarations", library_->enum_declarations_);
    GenerateObjectMember("experimental_resource_declarations", library_->resource_declarations_);
    GenerateObjectMember("interface_declarations", library_->protocol_declarations_);
    GenerateObjectMember("service_declarations", library_->service_declarations_);
    GenerateObjectMember("struct_declarations", AllStructs(library_));
    GenerateObjectMember("table_declarations", library_->table_declarations_);
    GenerateObjectMember("union_declarations", library_->union_declarations_);
    GenerateObjectMember("type_alias_declarations", library_->type_alias_declarations_);

    // The library's declaration_order_ contains all the declarations for all
    // transitive dependencies. The backend only needs the declaration order
    // for this specific library.
    std::vector<std::string> declaration_order;
    for (const flat::Decl* decl : library_->declaration_order_) {
      if (decl->kind == flat::Decl::Kind::kStruct) {
        auto struct_decl = static_cast<const flat::Struct*>(decl);
        if (struct_decl->is_request_or_response)
          continue;
      }
      if (decl->name.library() == library_)
        declaration_order.push_back(NameFlatName(decl->name));
    }
    GenerateObjectMember("declaration_order", declaration_order);

    GenerateDeclarationsMember(library_);
  });
  GenerateEOF();

  return std::move(json_file_);
}

}  // namespace fidl
