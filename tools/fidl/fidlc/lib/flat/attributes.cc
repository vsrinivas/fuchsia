// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/attributes.h"

#include "fidl/flat/compile_step.h"
#include "fidl/flat/typespace.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

using namespace diagnostics;

const AttributeArg* Attribute::GetArg(std::string_view arg_name) const {
  std::string name = utils::canonicalize(arg_name);
  for (const auto& arg : args) {
    if (arg->name.value().data() == name) {
      return arg.get();
    }
  }
  return nullptr;
}

AttributeArg* Attribute::GetStandaloneAnonymousArg() const {
  assert(!compiled &&
         "if calling after attribute compilation, use GetArg(...) with the resolved name instead");
  if (args.size() == 1 && !args[0]->name.has_value()) {
    return args[0].get();
  }
  return nullptr;
}

const Attribute* AttributeList::Get(std::string_view attribute_name) const {
  for (const auto& attribute : attributes) {
    if (attribute->name.data() == attribute_name)
      return attribute.get();
  }
  return nullptr;
}

Attribute* AttributeList::Get(std::string_view attribute_name) {
  for (const auto& attribute : attributes) {
    if (attribute->name.data() == attribute_name)
      return attribute.get();
  }
  return nullptr;
}

AttributeSchema& AttributeSchema::RestrictTo(std::set<AttributePlacement> placements) {
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
                               const Attributable* attributable) const {
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
      if (specific_placements_.count(attributable->placement) == 0) {
        reporter->Fail(ErrInvalidAttributePlacement, attribute->span, attribute);
        return;
      }
      break;
    case Placement::kAnonymousLayout:
      switch (attributable->placement) {
        case AttributePlacement::kBitsDecl:
        case AttributePlacement::kEnumDecl:
        case AttributePlacement::kStructDecl:
        case AttributePlacement::kTableDecl:
        case AttributePlacement::kUnionDecl:
          if (static_cast<const Decl*>(attributable)->name.as_anonymous()) {
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
  auto passed = constraint_(reporter, attribute, attributable);
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
    anon_arg->name = step->library_->GeneratedSimpleName(arg_schemas_.begin()->first);
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
    anon_arg->name = step->library_->GeneratedSimpleName(AttributeArg::kDefaultAnonymousName);
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

}  // namespace fidl::flat
