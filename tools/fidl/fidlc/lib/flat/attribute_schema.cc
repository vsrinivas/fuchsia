// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/attribute_schema.h"

#include "fidl/flat/compile_step.h"
#include "fidl/flat/typespace.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

AttributeSchema& AttributeSchema::RestrictTo(std::set<Element::Kind> placements) {
  assert(!placements.empty() && "must allow some placements");
  assert(kind_ == AttributeSchema::Kind::kValidateOnly ||
         kind_ == AttributeSchema::Kind::kUseEarly ||
         kind_ == AttributeSchema::Kind::kCompileEarly && "wrong kind");
  assert(placement_ == AttributeSchema::Placement::kAnywhere && "already set placements");
  assert(specific_placements_.empty() && "already set placements");
  placement_ = AttributeSchema::Placement::kSpecific;
  specific_placements_ = std::move(placements);
  return *this;
}

AttributeSchema& AttributeSchema::RestrictToAnonymousLayouts() {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly ||
         kind_ == AttributeSchema::Kind::kUseEarly ||
         kind_ == AttributeSchema::Kind::kCompileEarly && "wrong kind");
  assert(placement_ == AttributeSchema::Placement::kAnywhere && "already set placements");
  assert(specific_placements_.empty() && "already set placements");
  placement_ = AttributeSchema::Placement::kAnonymousLayout;
  return *this;
}

AttributeSchema& AttributeSchema::AddArg(AttributeArgSchema arg_schema) {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly ||
         kind_ == AttributeSchema::Kind::kUseEarly ||
         kind_ == AttributeSchema::Kind::kCompileEarly && "wrong kind");
  assert(arg_schemas_.empty() && "can only have one unnamed arg");
  arg_schemas_.emplace(AttributeArg::kDefaultAnonymousName, arg_schema);
  return *this;
}

AttributeSchema& AttributeSchema::AddArg(std::string name, AttributeArgSchema arg_schema) {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly ||
         kind_ == AttributeSchema::Kind::kUseEarly ||
         kind_ == AttributeSchema::Kind::kCompileEarly && "wrong kind");
  [[maybe_unused]] const auto& [it, inserted] =
      arg_schemas_.try_emplace(std::move(name), arg_schema);
  assert(inserted && "duplicate argument name");
  return *this;
}

AttributeSchema& AttributeSchema::Constrain(AttributeSchema::Constraint constraint) {
  assert(constraint != nullptr && "constraint must be non-null");
  assert(constraint_ == nullptr && "already set constraint");
  assert(kind_ == AttributeSchema::Kind::kValidateOnly &&
         "constraints only allowed on kValidateOnly attributes");
  constraint_ = std::move(constraint);
  return *this;
}

AttributeSchema& AttributeSchema::UseEarly() {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly && "already changed kind");
  assert(constraint_ == nullptr && "use-early attribute should not specify constraint");
  kind_ = AttributeSchema::Kind::kUseEarly;
  return *this;
}

AttributeSchema& AttributeSchema::CompileEarly() {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly && "already changed kind");
  assert(constraint_ == nullptr && "compile-early attribute should not specify constraint");
  kind_ = AttributeSchema::Kind::kCompileEarly;
  return *this;
}

AttributeSchema& AttributeSchema::Deprecate() {
  assert(kind_ == AttributeSchema::Kind::kValidateOnly && "wrong kind");
  assert(placement_ == AttributeSchema::Placement::kAnywhere &&
         "deprecated attribute should not specify placement");
  assert(arg_schemas_.empty() && "deprecated attribute should not specify arguments");
  assert(constraint_ == nullptr && "deprecated attribute should not specify constraint");
  kind_ = AttributeSchema::Kind::kDeprecated;
  return *this;
}

// static
const AttributeSchema AttributeSchema::kUserDefined(Kind::kUserDefined);

void AttributeSchema::Validate(Reporter* reporter, const Attribute* attribute,
                               const Element* element) const {
  switch (kind_) {
    case Kind::kValidateOnly:
      break;
    case Kind::kUseEarly:
    case Kind::kCompileEarly:
      assert(constraint_ == nullptr &&
             "use-early and compile-early schemas should not have a constraint");
      break;
    case Kind::kDeprecated:
      reporter->Fail(ErrDeprecatedAttribute, attribute->span, attribute);
      return;
    case Kind::kUserDefined:
      return;
  }

  switch (placement_) {
    case Placement::kAnywhere:
      break;
    case Placement::kSpecific:
      if (specific_placements_.count(element->kind) == 0) {
        reporter->Fail(ErrInvalidAttributePlacement, attribute->span, attribute);
        return;
      }
      break;
    case Placement::kAnonymousLayout:
      switch (element->kind) {
        case Element::Kind::kBits:
        case Element::Kind::kEnum:
        case Element::Kind::kStruct:
        case Element::Kind::kTable:
        case Element::Kind::kUnion:
          if (static_cast<const Decl*>(element)->name.as_anonymous()) {
            // Good: the attribute is on an anonymous layout.
            break;
          }
          [[fallthrough]];
        default:
          reporter->Fail(ErrInvalidAttributePlacement, attribute->span, attribute);
          return;
      }
      break;
  }

  if (constraint_ == nullptr) {
    return;
  }
  auto check = reporter->Checkpoint();
  auto passed = constraint_(reporter, attribute, element);
  if (passed) {
    assert(check.NoNewErrors() && "cannot add errors and pass");
    return;
  }
  if (check.NoNewErrors()) {
    reporter->Fail(ErrAttributeConstraintNotSatisfied, attribute->span, attribute);
  }
}

void AttributeSchema::ResolveArgs(CompileStep* step, Attribute* attribute) const {
  switch (kind_) {
    case Kind::kValidateOnly:
    case Kind::kUseEarly:
    case Kind::kCompileEarly:
      break;
    case Kind::kDeprecated:
      // Don't attempt to resolve arguments, as we don't store arument schemas
      // for deprecated attributes. Instead, rely on AttributeSchema::Validate
      // to report the error.
      return;
    case Kind::kUserDefined:
      ResolveArgsWithoutSchema(step, attribute);
      return;
  }

  // Name the anonymous argument (if present).
  if (auto anon_arg = attribute->GetStandaloneAnonymousArg()) {
    if (arg_schemas_.empty()) {
      step->Fail(ErrAttributeDisallowsArgs, attribute->span, attribute);
      return;
    }
    if (arg_schemas_.size() > 1) {
      step->Fail(ErrAttributeArgNotNamed, attribute->span, anon_arg);
      return;
    }
    anon_arg->name = step->generated_source_file()->AddLine(arg_schemas_.begin()->first);
  } else if (arg_schemas_.size() == 1 && attribute->args.size() == 1) {
    step->Fail(ErrAttributeArgMustNotBeNamed, attribute->span);
  }

  // Resolve each argument by name.
  for (auto& arg : attribute->args) {
    const auto it = arg_schemas_.find(arg->name.value().data());
    if (it == arg_schemas_.end()) {
      step->Fail(ErrUnknownAttributeArg, attribute->span, attribute, arg->name.value().data());
      continue;
    }
    const auto& [name, schema] = *it;
    const bool literal_only = kind_ == Kind::kCompileEarly;
    schema.ResolveArg(step, attribute, arg.get(), literal_only);
  }

  // Check for missing arguments.
  for (const auto& [name, schema] : arg_schemas_) {
    if (schema.IsOptional() || attribute->GetArg(name) != nullptr) {
      continue;
    }
    if (arg_schemas_.size() == 1) {
      step->Fail(ErrMissingRequiredAnonymousAttributeArg, attribute->span, attribute);
    } else {
      step->Fail(ErrMissingRequiredAttributeArg, attribute->span, attribute, name);
    }
  }
}

void AttributeArgSchema::ResolveArg(CompileStep* step, Attribute* attribute, AttributeArg* arg,
                                    bool literal_only) const {
  Constant* constant = arg->value.get();

  if (literal_only && constant->kind != Constant::Kind::kLiteral) {
    step->Fail(ErrAttributeArgRequiresLiteral, constant->span, arg->name.value().data(), attribute);
    return;
  }

  const Type* target_type;
  switch (type_) {
    case ConstantValue::Kind::kDocComment:
      assert(false && "we know the target type of doc comments, and should not end up here");
      return;
    case ConstantValue::Kind::kString:
      target_type = &Typespace::kUnboundedStringType;
      break;
    case ConstantValue::Kind::kBool:
      target_type = &Typespace::kBoolType;
      break;
    case ConstantValue::Kind::kInt8:
      target_type = &Typespace::kInt8Type;
      break;
    case ConstantValue::Kind::kInt16:
      target_type = &Typespace::kInt16Type;
      break;
    case ConstantValue::Kind::kInt32:
      target_type = &Typespace::kInt32Type;
      break;
    case ConstantValue::Kind::kInt64:
      target_type = &Typespace::kInt64Type;
      break;
    case ConstantValue::Kind::kUint8:
      target_type = &Typespace::kUint8Type;
      break;
    case ConstantValue::Kind::kUint16:
      target_type = &Typespace::kUint16Type;
      break;
    case ConstantValue::Kind::kUint32:
      target_type = &Typespace::kUint32Type;
      break;
    case ConstantValue::Kind::kUint64:
      target_type = &Typespace::kUint64Type;
      break;
    case ConstantValue::Kind::kFloat32:
      target_type = &Typespace::kFloat32Type;
      break;
    case ConstantValue::Kind::kFloat64:
      target_type = &Typespace::kFloat64Type;
      break;
  }
  if (!step->ResolveConstant(constant, target_type)) {
    step->Fail(ErrCouldNotResolveAttributeArg, arg->span);
  }
}

// static
void AttributeSchema::ResolveArgsWithoutSchema(CompileStep* step, Attribute* attribute) {
  // For attributes with a single, anonymous argument like `@foo("bar")`, assign
  // a default name so that arguments are always named after compilation.
  if (auto anon_arg = attribute->GetStandaloneAnonymousArg()) {
    anon_arg->name = step->generated_source_file()->AddLine(AttributeArg::kDefaultAnonymousName);
  }

  // Try resolving each argument as string or bool. We don't allow numerics
  // because it's not clear what type (int8, uint32, etc.) we should infer.
  for (const auto& arg : attribute->args) {
    assert(arg->value->kind != Constant::Kind::kBinaryOperator &&
           "attribute arg with a binary operator is a parse error");

    auto inferred_type = step->InferType(arg->value.get());
    if (!inferred_type) {
      step->Fail(ErrCouldNotResolveAttributeArg, attribute->span);
      continue;
    }
    // Only string or bool supported.
    switch (inferred_type->kind) {
      case Type::Kind::kString:
        break;
      case Type::Kind::kPrimitive:
        if (static_cast<const PrimitiveType*>(inferred_type)->subtype ==
            types::PrimitiveSubtype::kBool) {
          break;
        }
        [[fallthrough]];
      case Type::Kind::kIdentifier:
      case Type::Kind::kArray:
      case Type::Kind::kBox:
      case Type::Kind::kVector:
      case Type::Kind::kHandle:
      case Type::Kind::kTransportSide:
      case Type::Kind::kUntypedNumeric:
        step->Fail(ErrCanOnlyUseStringOrBool, attribute->span, arg.get(), attribute);
        continue;
    }
    if (!step->ResolveConstant(arg->value.get(), inferred_type)) {
      // Since we've inferred the type, it must resolve correctly.
      __builtin_unreachable();
    }
  }
}

static const std::set<std::pair<std::string, std::string_view>> allowed_simple_unions{{
    {"fuchsia.io", "NodeInfo"},
}};

static bool IsSimple(const Type* type, Reporter* reporter) {
  auto depth = fidl::OldWireFormatDepth(type);
  switch (type->kind) {
    case Type::Kind::kVector: {
      auto vector_type = static_cast<const VectorType*>(type);
      if (*vector_type->element_count == Size::Max())
        return false;
      switch (vector_type->element_type->kind) {
        case Type::Kind::kHandle:
        case Type::Kind::kTransportSide:
        case Type::Kind::kPrimitive:
          return true;
        case Type::Kind::kArray:
        case Type::Kind::kVector:
        case Type::Kind::kString:
        case Type::Kind::kIdentifier:
        case Type::Kind::kBox:
          return false;
        case Type::Kind::kUntypedNumeric:
          assert(false && "compiler bug: should not have untyped numeric here");
          return false;
      }
    }
    case Type::Kind::kString: {
      auto string_type = static_cast<const StringType*>(type);
      return *string_type->max_size < Size::Max();
    }
    case Type::Kind::kArray:
    case Type::Kind::kHandle:
    case Type::Kind::kTransportSide:
    case Type::Kind::kPrimitive:
      return depth == 0u;
    case Type::Kind::kIdentifier: {
      auto identifier_type = static_cast<const IdentifierType*>(type);
      if (identifier_type->type_decl->kind == Decl::Kind::kUnion) {
        auto name = identifier_type->type_decl->name;
        auto union_name = std::make_pair<const std::string&, const std::string_view&>(
            LibraryName(name.library(), "."), name.decl_name());
        if (allowed_simple_unions.find(union_name) == allowed_simple_unions.end()) {
          // Any unions not in the allow-list are treated as non-simple.
          return reporter->Fail(ErrUnionCannotBeSimple, name.span().value(), name);
        }
      }
      // TODO(fxbug.dev/70186): This only applies to nullable structs, which should
      // be handled as box.
      switch (identifier_type->nullability) {
        case types::Nullability::kNullable:
          // If the identifier is nullable, then we can handle a depth of 1
          // because the secondary object is directly accessible.
          return depth <= 1u;
        case types::Nullability::kNonnullable:
          return depth == 0u;
      }
    }
    case Type::Kind::kBox:
      // we can handle a depth of 1 because the secondary object is directly accessible.
      return depth <= 1u;
    case Type::Kind::kUntypedNumeric:
      assert(false && "compiler bug: should not have untyped numeric here");
      return false;
  }
}

static bool SimpleLayoutConstraint(Reporter* reporter, const Attribute* attr,
                                   const Element* element) {
  assert(element);
  bool ok = true;
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!SimpleLayoutConstraint(reporter, attr, method)) {
          ok = false;
        }
      }
      break;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!SimpleLayoutConstraint(reporter, attr, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!SimpleLayoutConstraint(reporter, attr, as_struct)) {
          ok = false;
        }
      }
      break;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      for (const auto& member : struct_decl->members) {
        if (!IsSimple(member.type_ctor->type, reporter)) {
          reporter->Fail(ErrMemberMustBeSimple, member.name, member.name.data());
          ok = false;
        }
      }
      break;
    }
    default:
      assert(false && "unexpected kind");
  }
  return ok;
}

static bool ParseBound(Reporter* reporter, const Attribute* attribute, std::string_view input,
                       uint32_t* out_value) {
  auto result = utils::ParseNumeric(input, out_value, 10);
  switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
      reporter->Fail(ErrBoundIsTooBig, attribute->span, attribute, input);
      return false;
    case utils::ParseNumericResult::kMalformed: {
      reporter->Fail(ErrUnableToParseBound, attribute->span, attribute, input);
      return false;
    }
    case utils::ParseNumericResult::kSuccess:
      return true;
  }
}

static bool MaxBytesConstraint(Reporter* reporter, const Attribute* attribute,
                               const Element* element) {
  assert(element);
  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      bool ok = true;
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!MaxBytesConstraint(reporter, attribute, method)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      bool ok = true;
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxBytesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxBytesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      max_bytes = struct_decl->typeshape(WireFormat::kV1NoEe).inline_size +
                  struct_decl->typeshape(WireFormat::kV1NoEe).max_out_of_line;
      break;
    }
    case Element::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(element);
      max_bytes = table_decl->typeshape(WireFormat::kV1NoEe).inline_size +
                  table_decl->typeshape(WireFormat::kV1NoEe).max_out_of_line;
      break;
    }
    case Element::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(element);
      max_bytes = union_decl->typeshape(WireFormat::kV1NoEe).inline_size +
                  union_decl->typeshape(WireFormat::kV1NoEe).max_out_of_line;
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_bytes > bound) {
    reporter->Fail(ErrTooManyBytes, attribute->span, bound, max_bytes);
    return false;
  }
  return true;
}

static bool MaxHandlesConstraint(Reporter* reporter, const Attribute* attribute,
                                 const Element* element) {
  assert(element);
  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  uint32_t bound;
  if (!ParseBound(reporter, attribute, std::string(arg_value.MakeContents()), &bound))
    return false;
  uint32_t max_handles = std::numeric_limits<uint32_t>::max();
  switch (element->kind) {
    case Element::Kind::kProtocol: {
      auto protocol = static_cast<const Protocol*>(element);
      bool ok = true;
      for (const auto& method_with_info : protocol->all_methods) {
        auto* method = method_with_info.method;
        if (!MaxHandlesConstraint(reporter, attribute, method)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kProtocolMethod: {
      auto method = static_cast<const Protocol::Method*>(element);
      bool ok = true;
      if (method->maybe_request) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_request->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxHandlesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      if (method->maybe_response) {
        auto id = static_cast<const flat::IdentifierType*>(method->maybe_response->type);

        // TODO(fxbug.dev/88343): switch on union/table when those are enabled.
        auto as_struct = static_cast<const flat::Struct*>(id->type_decl);
        if (!MaxHandlesConstraint(reporter, attribute, as_struct)) {
          ok = false;
        }
      }
      return ok;
    }
    case Element::Kind::kStruct: {
      auto struct_decl = static_cast<const Struct*>(element);
      max_handles = struct_decl->typeshape(WireFormat::kV1NoEe).max_handles;
      break;
    }
    case Element::Kind::kTable: {
      auto table_decl = static_cast<const Table*>(element);
      max_handles = table_decl->typeshape(WireFormat::kV1NoEe).max_handles;
      break;
    }
    case Element::Kind::kUnion: {
      auto union_decl = static_cast<const Union*>(element);
      max_handles = union_decl->typeshape(WireFormat::kV1NoEe).max_handles;
      break;
    }
    default:
      assert(false && "unexpected kind");
      return false;
  }
  if (max_handles > bound) {
    reporter->Fail(ErrTooManyHandles, attribute->span, bound, max_handles);
    return false;
  }
  return true;
}

static bool ResultShapeConstraint(Reporter* reporter, const Attribute* attribute,
                                  const Element* element) {
  assert(element);
  assert(element->kind == Element::Kind::kUnion);
  auto union_decl = static_cast<const Union*>(element);
  assert(union_decl->members.size() == 2);
  auto& error_member = union_decl->members.at(1);
  assert(error_member.maybe_used && "must have an error member");
  auto error_type = error_member.maybe_used->type_ctor->type;

  const PrimitiveType* error_primitive = nullptr;
  if (error_type->kind == Type::Kind::kPrimitive) {
    error_primitive = static_cast<const PrimitiveType*>(error_type);
  } else if (error_type->kind == Type::Kind::kIdentifier) {
    auto identifier_type = static_cast<const IdentifierType*>(error_type);
    if (identifier_type->type_decl->kind == Decl::Kind::kEnum) {
      auto error_enum = static_cast<const Enum*>(identifier_type->type_decl);
      assert(error_enum->subtype_ctor->type->kind == Type::Kind::kPrimitive);
      error_primitive = static_cast<const PrimitiveType*>(error_enum->subtype_ctor->type);
    }
  }

  if (!error_primitive || (error_primitive->subtype != types::PrimitiveSubtype::kInt32 &&
                           error_primitive->subtype != types::PrimitiveSubtype::kUint32)) {
    reporter->Fail(ErrInvalidErrorType, union_decl->name.span().value());
    return false;
  }

  return true;
}

static std::string Trim(std::string s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
            return !utils::IsWhitespace(static_cast<char>(ch));
          }));
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](int ch) { return !utils::IsWhitespace(static_cast<char>(ch)); })
              .base(),
          s.end());
  return s;
}

static bool TransportConstraint(Reporter* reporter, const Attribute* attribute,
                                const Element* element) {
  assert(element);
  assert(element->kind == Element::Kind::kProtocol);

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kValidTransports = new std::set<std::string>{
      "Banjo",
      "Channel",
      "Driver",
      "Syscall",
  };

  auto arg = attribute->GetArg(AttributeArg::kDefaultAnonymousName);
  auto arg_value = static_cast<const flat::StringConstantValue&>(arg->value->Value());

  // Parse comma separated transports
  const std::string& value = arg_value.MakeContents();
  std::string::size_type prev_pos = 0;
  std::string::size_type pos;
  std::vector<std::string> transports;
  while ((pos = value.find(',', prev_pos)) != std::string::npos) {
    transports.emplace_back(Trim(value.substr(prev_pos, pos - prev_pos)));
    prev_pos = pos + 1;
  }
  transports.emplace_back(Trim(value.substr(prev_pos)));

  // Validate that they're ok
  for (const auto& transport : transports) {
    if (kValidTransports->count(transport) == 0) {
      reporter->Fail(ErrInvalidTransportType, attribute->span, transport, *kValidTransports);
      return false;
    }
  }
  return true;
}

// static
AttributeSchemaMap AttributeSchema::OfficialAttributes() {
  AttributeSchemaMap map;
  map["discoverable"].RestrictTo({
      Element::Kind::kProtocol,
  });
  map[std::string(Attribute::kDocCommentName)].AddArg(
      AttributeArgSchema(ConstantValue::Kind::kString));
  map["layout"].Deprecate();
  map["for_deprecated_c_bindings"]
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kStruct,
      })
      .Constrain(SimpleLayoutConstraint);
  map["generated_name"]
      .RestrictToAnonymousLayouts()
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .CompileEarly();
  map["max_bytes"]
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kProtocolMethod,
          Element::Kind::kStruct,
          Element::Kind::kTable,
          Element::Kind::kUnion,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(MaxBytesConstraint);
  map["max_handles"]
      .RestrictTo({
          Element::Kind::kProtocol,
          Element::Kind::kProtocolMethod,
          Element::Kind::kStruct,
          Element::Kind::kTable,
          Element::Kind::kUnion,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(MaxHandlesConstraint);
  map["result"]
      .RestrictTo({
          Element::Kind::kUnion,
      })
      .Constrain(ResultShapeConstraint);
  map["selector"]
      .RestrictTo({
          Element::Kind::kProtocolMethod,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .UseEarly();
  map["transitional"]
      .RestrictTo({
          Element::Kind::kProtocolMethod,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString,
                                 AttributeArgSchema::Optionality::kOptional));
  map["transport"]
      .RestrictTo({
          Element::Kind::kProtocol,
      })
      .AddArg(AttributeArgSchema(ConstantValue::Kind::kString))
      .Constrain(TransportConstraint);
  map["unknown"].RestrictTo({Element::Kind::kEnumMember});
  return map;
}

}  // namespace fidl::flat
