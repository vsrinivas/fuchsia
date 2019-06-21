// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format.h"

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/format_expr_value_options.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/expr/resolve_variant.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using NumFormat = FormatExprValueOptions::NumFormat;

// Returns true if the base type is some kind of number such that the NumFormat
// of the format options should be applied.
bool IsNumericBaseType(int base_type) {
  return base_type == BaseType::kBaseTypeSigned ||
         base_type == BaseType::kBaseTypeUnsigned ||
         base_type == BaseType::kBaseTypeBoolean ||
         base_type == BaseType::kBaseTypeFloat ||
         base_type == BaseType::kBaseTypeSignedChar ||
         base_type == BaseType::kBaseTypeUnsignedChar ||
         base_type == BaseType::kBaseTypeUTF;
}

// Appends the given byte to the destination, escaping as per C rules.
void AppendEscapedChar(uint8_t ch, std::string* dest) {
  if (ch == '\'' || ch == '\"' || ch == '\\') {
    // These characters get backslash-escaped.
    dest->push_back('\\');
    dest->push_back(ch);
  } else if (ch == '\n') {
    dest->append("\\n");
  } else if (ch == '\r') {
    dest->append("\\r");
  } else if (ch == '\t') {
    dest->append("\\t");
  } else if (isprint(ch)) {
    dest->push_back(ch);
  } else {
    // Hex-encode everything else.
    *dest += fxl::StringPrintf("\\x%02x", static_cast<unsigned>(ch));
  }
}

void FormatBoolean(FormatNode* node) {
  uint64_t int_val = 0;
  Err err = node->value().PromoteTo64(&int_val);
  if (err.has_error())
    node->set_err(err);
  else if (int_val)
    node->set_description("true");
  else
    node->set_description("false");
}

void FormatFloat(FormatNode* node) {
  const ExprValue& value = node->value();
  switch (node->value().data().size()) {
    case sizeof(float):
      node->set_description(fxl::StringPrintf("%g", value.GetAs<float>()));
      break;
    case sizeof(double):
      node->set_description(fxl::StringPrintf("%g", value.GetAs<double>()));
      break;
    default:
      node->set_err(Err(fxl::StringPrintf(
          "Unknown float of size %d", static_cast<int>(value.data().size()))));
      break;
  }
}

void FormatSignedInt(FormatNode* node) {
  int64_t int_val = 0;
  Err err = node->value().PromoteTo64(&int_val);
  if (err.has_error())
    node->set_err(err);
  else
    node->set_description(fxl::StringPrintf("%" PRId64, int_val));
}

void FormatUnsignedInt(FormatNode* node,
                       const FormatExprValueOptions& options) {
  // This formatter handles unsigned and hex output.
  uint64_t int_val = 0;
  Err err = node->value().PromoteTo64(&int_val);
  if (err.has_error())
    node->set_err(err);
  else if (options.num_format == NumFormat::kHex)
    node->set_description(fxl::StringPrintf("0x%" PRIx64, int_val));
  else
    node->set_description(fxl::StringPrintf("%" PRIu64, int_val));
}

void FormatChar(FormatNode* node) {
  // Just take the first byte for all char.
  // TODO(brettw) handle unicode, etc.
  if (node->value().data().empty()) {
    node->set_err(Err("Invalid char type"));
    return;
  }
  std::string str;
  str.push_back('\'');
  AppendEscapedChar(node->value().data()[0], &str);
  str.push_back('\'');
  node->set_description(std::move(str));
}

void FormatNumeric(FormatNode* node, const FormatExprValueOptions& options) {
  node->set_description_kind(FormatNode::kBaseType);

  if (options.num_format != NumFormat::kDefault) {
    // Overridden format option.
    switch (options.num_format) {
      case NumFormat::kUnsigned:
      case NumFormat::kHex:
        FormatUnsignedInt(node, options);
        break;
      case NumFormat::kSigned:
        FormatSignedInt(node);
        break;
      case NumFormat::kChar:
        FormatChar(node);
        break;
      case NumFormat::kDefault:
        // Prevent warning for unused enum type.
        break;
    }
  } else {
    // Default handling for base types based on the number.
    switch (node->value().GetBaseType()) {
      case BaseType::kBaseTypeBoolean:
        FormatBoolean(node);
        break;
      case BaseType::kBaseTypeFloat:
        FormatFloat(node);
        break;
      case BaseType::kBaseTypeSigned:
        FormatSignedInt(node);
        break;
      case BaseType::kBaseTypeUnsigned:
        FormatUnsignedInt(node, options);
        break;
      case BaseType::kBaseTypeSignedChar:
      case BaseType::kBaseTypeUnsignedChar:
      case BaseType::kBaseTypeUTF:
        FormatChar(node);
        break;
    }
  }
}

// Rust enums are formatted with the enum name in the description.
//
// The active variant will have a set of data members of which only one will be
// used. It will refer to a collection which will have the set of members.
// This structure will vary according to the type of enum:
//   EnumWithNoValue
//       The struct will have no members.
//   OneValue(u32)
//       The struct will have one member named __0
//   Tuple(u32, u32, etc.)
//       The struct will have two values, __0 and __1, etc.
//   Struct{x:u32, y:u32}
//       The struct will have "x" and "y" members.
void FormatRustEnum(FormatNode* node, const Collection* coll,
                    const FormatExprValueOptions& options,
                    fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kRustEnum);

  const VariantPart* variant_part = coll->variant_part().Get()->AsVariantPart();
  if (!variant_part) {
    node->set_err(Err("Missing variant part for Rust enum."));
    return;
  }

  fxl::RefPtr<Variant> variant;
  Err err = ResolveVariant(eval_context, node->value(), variant_part, &variant);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  // Add each variant data member as a child of this node. In Rust we expect
  // exactly one but it can't hurt to be general.
  std::string enum_name;
  for (const auto& lazy_member : variant->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    // Save the first member's name to be the name of the whole enum, even if
    // there are no data members. Normally there will be exactly one.
    if (enum_name.empty())
      enum_name = member->GetAssignedName();

    // TODO(brettw) this will append a child unconditionally.
    ExprValue member_value;
    err = ResolveMember(eval_context, node->value(), member, &member_value);
    if (err.has_error()) {
      // In the error case, still append a child so that the child can have
      // the error associated with it.
      node->children().push_back(
          std::make_unique<FormatNode>(member->GetAssignedName(), err));
    } else {
      // Only append as a child if the variant has "stuff". The case here is
      // to skip adding children for enums with no data like
      // "Optional<Foo>::None" which will have a struct called "None" with no
      // members.
      auto member_type = member_value.GetConcreteType(eval_context.get());
      const Collection* member_coll_type = member_type->AsCollection();
      if (!member_coll_type || !member_coll_type->data_members().empty()) {
        node->children().push_back(std::make_unique<FormatNode>(
            member->GetAssignedName(), std::move(member_value)));
      }
    }
  }

  // Name for the whole node.
  node->set_description(enum_name);
}

void FormatCollection(FormatNode* node, const Collection* coll,
                      const FormatExprValueOptions& options,
                      fxl::RefPtr<EvalContext> eval_context) {
  if (coll->is_declaration()) {
    // Sometimes a value will have a type that's a forward declaration and we
    // couldn't resolve its concrete type. Print an error instead of "{}".
    node->set_err(Err("No definition."));
    return;
  }

  // Special-case Rust enums which are encoded as a type of collection.
  Collection::SpecialType special_type = coll->GetSpecialType();
  if (special_type == Collection::kRustEnum) {
    FormatRustEnum(node, coll, options, std::move(eval_context));
    return;
  }

  // Base classes.
  for (const auto& lazy_inherited : coll->inherited_from()) {
    const InheritedFrom* inherited = lazy_inherited.Get()->AsInheritedFrom();
    if (!inherited)
      continue;

    const Collection* from = inherited->from().Get()->AsCollection();
    if (!from)
      continue;

    // Some base classes are empty. Only show if this base class or any of
    // its base classes have member values.
    VisitResult has_members_result =
        VisitClassHierarchy(from, [](const Collection* cur, uint64_t) {
          if (cur->data_members().empty())
            return VisitResult::kContinue;
          return VisitResult::kDone;
        });
    if (has_members_result == VisitResult::kContinue)
      continue;

    // Derived class nodes are named by the type of the base class.
    std::string from_name = from->GetFullName();

    ExprValue from_value;
    Err err = ResolveInherited(node->value(), inherited, &from_value);
    if (err.has_error()) {
      node->children().push_back(std::make_unique<FormatNode>(from_name, err));
    } else {
      node->children().push_back(
          std::make_unique<FormatNode>(from_name, from_value));
    }
  }

  // Data members.
  for (const auto& lazy_member : coll->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    std::string member_name = member->GetAssignedName();

    ExprValue member_value;
    Err err = ResolveMember(eval_context, node->value(), member, &member_value);
    if (err.has_error()) {
      node->children().push_back(
          std::make_unique<FormatNode>(member_name, err));
    } else {
      node->children().push_back(
          std::make_unique<FormatNode>(member_name, member_value));
    }
  }

  node->set_description_kind(FormatNode::kCollection);
}

void FormatPointer(FormatNode* node, const FormatExprValueOptions& options,
                   fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kPointer);

  // Note: don't make assumptions about the type of value.type() since it isn't
  // necessarily a ModifiedType representing a pointer, but could be other
  // things like a pointer to a member.

  Err err = node->value().EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  // The address goes in the description.
  node->set_description(
      fxl::StringPrintf("0x%" PRIx64, node->value().GetAs<TargetPointer>()));

  // Make a child node that's the dereferenced pointer value. If/when we
  // support GUIs, we should probably remove the intermediate node and put the
  // dereferenced struct members directly as children on this node. Otherwise
  // it's an annoying extra step to expand to things.

  // Use our name but with a "*" to show it dereferenced.
  auto deref_node = std::make_unique<FormatNode>(
      "*" + node->name(),
      [ptr_value = node->value()](
          fxl::RefPtr<EvalContext> context,
          std::function<void(const Err& err, ExprValue value)> cb) {
        ResolvePointer(context, ptr_value, std::move(cb));
      });
  node->children().push_back(std::move(deref_node));
}

}  // namespace

void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context,
                         std::function<void()> cb) {
  switch (node->source()) {
    case FormatNode::kValue:
      // Already has the value.
      cb();
      return;
    case FormatNode::kExpression:
      // Evaluate the expression.
      EvalExpression(node->expression(), context, true,
                     [weak_node = node->GetWeakPtr(), cb = std::move(cb)](
                         const Err& err, ExprValue value) {
                       if (!weak_node)
                         return;
                       if (err.has_error()) {
                         weak_node->set_err(err);
                         weak_node->SetValue(ExprValue());
                       } else {
                         weak_node->SetValue(std::move(value));
                       }
                       cb();
                     });
      return;
    case FormatNode::kProgramatic:
      // Lambda provides the value.
      node->FillProgramaticValue(std::move(context), std::move(cb));
      return;
  }
  FXL_NOTREACHED();
}

void FillFormatNodeDescription(FormatNode* node,
                               const FormatExprValueOptions& options,
                               fxl::RefPtr<EvalContext> context) {
  if (node->state() == FormatNode::kEmpty ||
      node->state() == FormatNode::kUnevaluated || node->err().has_error()) {
    node->set_state(FormatNode::kDescribed);
    return;
  }

  // All code paths below convert to "described" state.
  node->set_state(FormatNode::kDescribed);
  node->set_description(std::string());
  node->set_description_kind(FormatNode::kNone);
  node->children().clear();
  node->set_err(Err());

  // Format type name.
  if (!node->value().type()) {
    node->set_err(Err("No type"));
    return;
  }
  node->set_type(node->value().type()->GetFullName());

  // Trim "const", "volatile", etc. and follow typedef and using for the type
  // checking below.
  //
  // Always use this variable below instead of value.type().
  fxl::RefPtr<Type> type = node->value().GetConcreteType(context.get());

  // TODO(brettw) handle references here.

  if (const ModifiedType* modified_type = type->AsModifiedType()) {
    // Modified types (references were handled above).
    switch (modified_type->tag()) {
      case DwarfTag::kPointerType:
        // Function pointers need special handling.
        /* TODO(brettw) implement this.
        if (IsPointerToFunction(modified_type))
          FormatFunctionPointer(value, options, &out);
        else*/
        FormatPointer(node, options, context);
        break;
      default:
        node->set_err(Err("Unhandled type modifier 0x%x, please file a bug.",
                          static_cast<unsigned>(modified_type->tag())));
        break;
    }
  } else if (IsNumericBaseType(node->value().GetBaseType())) {
    // Numeric types.
    FormatNumeric(node, options);
  } else if (const Collection* coll = type->AsCollection()) {
    FormatCollection(node, coll, options, context);
  } else {
    node->set_err(Err("Unsupported type for new formatting system."));
  }
}

}  // namespace zxdb
