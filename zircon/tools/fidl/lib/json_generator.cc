// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/json_generator.h"

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
    // TODO(pascallouis): We should explore exposing these in the JSON IR, such that the
    // implicit bounds are made explicit by fidlc, rather than sprinkled throughout all
    // backends.
    //
    // For now, do not emit synthesized constants
    if (value.kind == flat::Constant::Kind::kSynthesized)
      return;
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
      case flat::Constant::Kind::kSynthesized:
        break;
    }
  });
}

void JSONGenerator::Generate(const flat::Type* value) {
  GenerateObject([&]() {
    GenerateObjectMember("kind", NameFlatTypeKind(value->kind), Position::kFirst);

    switch (value->kind) {
      case flat::Type::Kind::kArray: {
        auto type = static_cast<const flat::ArrayType*>(value);
        GenerateObjectMember("element_type", type->element_type);
        GenerateObjectMember("element_count", type->element_count->value);
        break;
      }
      case flat::Type::Kind::kVector: {
        auto type = static_cast<const flat::VectorType*>(value);
        GenerateObjectMember("element_type", type->element_type);
        if (*type->element_count < flat::Size::Max())
          GenerateObjectMember("maybe_element_count", type->element_count->value);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kString: {
        auto type = static_cast<const flat::StringType*>(value);
        if (*type->max_size < flat::Size::Max())
          GenerateObjectMember("maybe_element_count", type->max_size->value);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kHandle: {
        auto type = static_cast<const flat::HandleType*>(value);
        GenerateObjectMember("subtype", type->subtype);
        GenerateObjectMember(
            "rights",
            static_cast<const flat::NumericConstantValue<uint32_t>&>(type->rights->Value()).value);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kRequestHandle: {
        auto type = static_cast<const flat::RequestHandleType*>(value);
        GenerateObjectMember("subtype", type->protocol_type->name);
        // TODO(fxb/43803) Add required and optional rights.
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
      case flat::Type::Kind::kPrimitive: {
        auto type = static_cast<const flat::PrimitiveType*>(value);
        GenerateObjectMember("subtype", type->name);
        break;
      }
      case flat::Type::Kind::kIdentifier: {
        auto type = static_cast<const flat::IdentifierType*>(value);
        GenerateObjectMember("identifier", type->name);
        GenerateObjectMember("nullable", type->nullability);
        break;
      }
    }
  });
}

void JSONGenerator::Generate(const raw::Attribute& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    if (value.value != "")
      GenerateObjectMember("value", value.value);
    else
      GenerateObjectMember("value", std::string_view());
  });
}

void JSONGenerator::Generate(const raw::AttributeList& value) { Generate(value.attributes); }

void JSONGenerator::Generate(const raw::Ordinal64& value) { EmitNumeric(value.value); }

void JSONGenerator::Generate(const flat::Name& value) {
  // These look like (when there is a library)
  //     { "LIB.LIB.LIB", "ID" }
  // or (when there is not)
  //     { "ID" }
  Generate(NameFlatName(value));
}

void JSONGenerator::Generate(const flat::Bits& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(*value.subtype_ctor);
    // TODO(FIDL-324): When all numbers are wrapped as string, we can simply
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
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Const& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(*value.type_ctor);
    GenerateObjectMember("value", value.value);
  });
}

void JSONGenerator::Generate(const flat::Enum& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    // TODO(FIDL-324): Due to legacy reasons, the 'type' of enums is actually
    // the primitive subtype, and therefore cannot use
    // GenerateTypeAndFromTypeAlias here.
    GenerateObjectMember("type", value.type->name);
    if (value.subtype_ctor->from_type_alias)
      GenerateObjectMember("experimental_maybe_from_type_alias",
                           value.subtype_ctor->from_type_alias.value());
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
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Protocol& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("methods", value.all_methods);
  });
}

void JSONGenerator::Generate(const flat::Protocol::MethodWithInfo& method_with_info) {
  assert(method_with_info.method != nullptr);
  const auto& value = *method_with_info.method;
  GenerateObject([&]() {
    GenerateObjectMember("ordinal", value.generated_ordinal64, Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("has_request", value.maybe_request != nullptr);
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.maybe_request != nullptr) {
      GenerateRequest("maybe_request", *value.maybe_request);
    }
    GenerateObjectMember("has_response", value.maybe_response != nullptr);
    if (value.maybe_response != nullptr) {
      GenerateRequest("maybe_response", *value.maybe_response);
    }
    GenerateObjectMember("is_composed", method_with_info.is_composed);
  });
}

void JSONGenerator::GenerateTypeAndFromTypeAlias(const flat::TypeConstructor& value,
                                                 Position position) {
  GenerateObjectMember("type", value.type, position);
  if (value.from_type_alias)
    GenerateObjectMember("experimental_maybe_from_type_alias", value.from_type_alias.value());
}

void JSONGenerator::GenerateRequest(const std::string& prefix, const flat::Struct& value) {
  // Temporarily hardcode the generation of request/response struct members to use the old
  // wire format, in order to maintain compatibility during the transition for fxb/7704.
  // This block of code is copied from JsonWriter::GenerateArray (with the difference
  // noted below), and will be removed once backends are updated to use anonymous structs.
  GenerateObjectPunctuation(Position::kSubsequent);
  EmitObjectKey(prefix);
  EmitArrayBegin();
  if (value.members.begin() != value.members.end()) {
    Indent();
    EmitNewlineWithIndent();
  }
  for (auto it = value.members.begin(); it != value.members.end(); ++it) {
    if (it != value.members.begin())
      EmitArraySeparator();
    // call Generate with is_request_response = true on each struct member
    Generate(*it, true);
  }
  if (value.members.begin() != value.members.end()) {
    Outdent();
    EmitNewlineWithIndent();
  }
  EmitArrayEnd();

  if (!value.members.empty()) {
    GenerateObjectMember(prefix + "_payload", value.name);
  }
  GenerateTypeShapes(prefix, value, true);
}

void JSONGenerator::Generate(const flat::Resource::Property& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateTypeAndFromTypeAlias(*value.type_ctor);
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Resource& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateTypeAndFromTypeAlias(*value.subtype_ctor);
    GenerateObjectMember("properties", value.properties);
  });
}

void JSONGenerator::Generate(const flat::Service& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
  });
}

void JSONGenerator::Generate(const flat::Service::Member& value) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(*value.type_ctor, Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
  });
}

void JSONGenerator::Generate(const flat::Struct& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    GenerateObjectMember("anonymous", value.is_request_or_response);
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("members", value.members);
    GenerateObjectMember("resource", value.resourceness == types::Resourceness::kResource);
    GenerateTypeShapes(value);
  });
}

void JSONGenerator::Generate(const flat::Struct* value) { Generate(*value); }

void JSONGenerator::Generate(const flat::Struct::Member& value, bool is_request_or_response) {
  GenerateObject([&]() {
    GenerateTypeAndFromTypeAlias(*value.type_ctor, Position::kFirst);
    GenerateObjectMember("name", value.name);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    if (value.maybe_default_value)
      GenerateObjectMember("maybe_default_value", value.maybe_default_value);
    GenerateFieldShapes(value, is_request_or_response);
  });
}

void JSONGenerator::Generate(const flat::Table& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
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
      GenerateTypeAndFromTypeAlias(*value.maybe_used->type_ctor);
      GenerateObjectMember("name", value.maybe_used->name);
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
      if (value.maybe_used->attributes)
        GenerateObjectMember("maybe_attributes", value.maybe_used->attributes);
      // TODO(FIDL-609): Support defaults on tables.
    } else {
      assert(value.span);
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
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
    GenerateObjectMember("is_resource", type_shape.is_resource);
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
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
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
      GenerateTypeAndFromTypeAlias(*value.maybe_used->type_ctor);
      GenerateObjectMember("location", NameSpan(value.maybe_used->name));
      if (value.maybe_used->attributes)
        GenerateObjectMember("maybe_attributes", value.maybe_used->attributes);
    } else {
      GenerateObjectMember("reserved", true);
      GenerateObjectMember("location", NameSpan(value.span.value()));
    }
  });
}

void JSONGenerator::Generate(const flat::TypeConstructor::FromTypeAlias& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.decl->name, Position::kFirst);
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("args");

    // In preparation of template support, it is better to expose a
    // heterogenous argument list to backends, rather than the currently
    // limited internal view.
    EmitArrayBegin();
    if (value.maybe_arg_type) {
      Indent();
      EmitNewlineWithIndent();
      Generate(value.maybe_arg_type->name);
      Outdent();
      EmitNewlineWithIndent();
    }
    EmitArrayEnd();

    GenerateObjectMember("nullable", value.nullability);

    if (value.maybe_size)
      GenerateObjectMember("maybe_size", *value.maybe_size);
  });
}

void JSONGenerator::Generate(const flat::TypeConstructor& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.type ? value.type->name : value.name, Position::kFirst);
    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("args");

    // In preparation of template support, it is better to expose a
    // heterogenous argument list to backends, rather than the currently
    // limited internal view.
    EmitArrayBegin();
    if (value.maybe_arg_type_ctor) {
      Indent();
      EmitNewlineWithIndent();
      Generate(*value.maybe_arg_type_ctor);
      Outdent();
      EmitNewlineWithIndent();
    }
    EmitArrayEnd();

    GenerateObjectMember("nullable", value.nullability);

    if (value.maybe_size)
      GenerateObjectMember("maybe_size", value.maybe_size);
    if (value.handle_subtype)
      GenerateObjectMember("maybe_handle_subtype", value.handle_subtype.value());
    if (value.handle_rights)
      GenerateObjectMember("handle_rights", value.handle_rights);
  });
}

void JSONGenerator::Generate(const flat::TypeAlias& value) {
  GenerateObject([&]() {
    GenerateObjectMember("name", value.name, Position::kFirst);
    GenerateObjectMember("location", NameSpan(value.name));
    if (value.attributes)
      GenerateObjectMember("maybe_attributes", value.attributes);
    GenerateObjectMember("partial_type_ctor", *value.partial_type_ctor);
  });
}

void JSONGenerator::Generate(const flat::Library* library) {
  GenerateObject([&]() {
    auto library_name = flat::LibraryName(library, ".");
    GenerateObjectMember("name", library_name, Position::kFirst);
    GenerateDeclarationsMember(library);
  });
}

void JSONGenerator::GenerateTypeShapes(const flat::Object& object) {
  GenerateTypeShapes("", object);
}

void JSONGenerator::GenerateTypeShapes(std::string prefix, const flat::Object& object,
                                       bool is_request_or_response) {
  if (prefix.size() > 0) {
    prefix.push_back('_');
  }

  // NOTE: while the transition for fxb/7024 is ongoing, we need to treat request/responses
  // specially as before, but this will be removed once the transition is complete
  const auto& v1 = is_request_or_response ? WireFormat::kV1NoEe : WireFormat::kV1Header;
  GenerateObjectMember(prefix + "type_shape_v1", TypeShape(object, v1));
}

void JSONGenerator::GenerateFieldShapes(const flat::Struct::Member& struct_member,
                                        bool is_request_or_response) {
  // NOTE: while the transition for fxb/7024 is ongoing, we need to treat request/responses
  // specially as before, but this will be removed once the transition is complete
  const auto& v1 = is_request_or_response ? WireFormat::kV1NoEe : WireFormat::kV1Header;
  GenerateObjectMember("field_shape_v1", FieldShape(struct_member, v1));
}

void JSONGenerator::GenerateDeclarationsEntry(int count, const flat::Name& name,
                                              std::string_view decl) {
  if (count == 0) {
    Indent();
    EmitNewlineWithIndent();
  } else {
    EmitObjectSeparator();
  }
  EmitObjectKey(NameFlatName(name));
  EmitString(decl);
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

namespace {

struct LibraryComparator {
  bool operator()(const flat::Library* lhs, const flat::Library* rhs) const {
    assert(!lhs->name().empty());
    assert(!rhs->name().empty());
    return lhs->name() < rhs->name();
  }
};

std::set<const flat::Library*, LibraryComparator> TransitiveDependencies(
    const flat::Library* library) {
  std::set<const flat::Library*, LibraryComparator> dependencies;
  auto add_dependency = [&](const flat::Library* dep_library) {
    if (!dep_library->HasAttribute("Internal")) {
      dependencies.insert(dep_library);
    }
  };
  for (const auto& dep_library : library->dependencies()) {
    add_dependency(dep_library);
  }
  // Discover additional dependencies that are required to support
  // cross-library protocol composition.
  for (const auto& protocol : library->protocol_declarations_) {
    for (const auto method_with_info : protocol->all_methods) {
      if (auto request = method_with_info.method->maybe_request) {
        for (const auto& member : request->members) {
          if (auto dep_library = member.type_ctor->name.library()) {
            add_dependency(dep_library);
          }
        }
      }
      if (auto response = method_with_info.method->maybe_response) {
        for (const auto& member : response->members) {
          if (auto dep_library = member.type_ctor->name.library()) {
            add_dependency(dep_library);
          }
        }
      }
      add_dependency(method_with_info.method->owning_protocol->name.library());
    }
  }
  dependencies.erase(library);
  return dependencies;
}

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
      if (method->maybe_request && !method->maybe_request->members.empty()) {
        all_structs.push_back(method->maybe_request);
      }
      if (method->maybe_response && !method->maybe_response->members.empty()) {
        all_structs.push_back(method->maybe_response);
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

    if (auto attributes = library_->attributes(); attributes) {
      GenerateObjectMember("maybe_attributes", *attributes);
    }

    GenerateObjectPunctuation(Position::kSubsequent);
    EmitObjectKey("library_dependencies");
    GenerateArray(TransitiveDependencies(library_));

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
    for (flat::Decl* decl : library_->declaration_order_) {
      if (decl->kind == flat::Decl::Kind::kStruct) {
        auto struct_decl = static_cast<flat::Struct*>(decl);
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
