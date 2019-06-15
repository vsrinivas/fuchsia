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
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
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

void FormatCollection(FormatNode* node,
                      const Collection* coll,
                      const FormatExprValueOptions& options,
                      fxl::RefPtr<EvalContext> eval_context) {
  if (coll->is_declaration()) {
    // Sometimes a value will have a type that's a forward declaration and we
    // couldn't resolve its concrete type. Print an error instead of "{}".
    node->set_err(Err("No definition."));
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
      node->children().push_back(std::make_unique<FormatNode>(from_name, from_value));
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
      node->children().push_back(std::make_unique<FormatNode>(member_name, err));
    } else {
      node->children().push_back(std::make_unique<FormatNode>(member_name, member_value));
    }
  }
}

}  // namespace

void FillFormatNode(FormatNode* node, fxl::RefPtr<EvalContext> context) {
  FXL_DCHECK(node->state() == FormatNode::kUnevaluated ||
             node->state() == FormatNode::kHasValue);

  if (node->state() == FormatNode::kUnevaluated) {
    FillFormatNodeValue(node, std::move(context));
    return;
  }
}

void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context) {
  EvalExpression(
      node->expression(), context, true,
      [weak_node = node->GetWeakPtr()](const Err& err, ExprValue value) {
        if (!weak_node)
          return;
        if (err.has_error()) {
          weak_node->set_err(err);
          weak_node->SetValue(ExprValue());
        } else {
          weak_node->SetValue(std::move(value));
        }
      });
}

void FillFormatNodeDescription(FormatNode* node,
                               const FormatExprValueOptions& options,
                               fxl::RefPtr<EvalContext> context) {
  if (node->state() == FormatNode::kEmpty ||
      node->state() == FormatNode::kUnevaluated) {
    node->set_state(FormatNode::kDescribed);
    return;
  }

  // All code paths below convert to "described" state.
  node->set_state(FormatNode::kDescribed);
  node->set_description(std::string());
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

  if (IsNumericBaseType(node->value().GetBaseType())) {
    // Numeric types.
    FormatNumeric(node, options);
  } else if (const Collection* coll = type->AsCollection()) {
    FormatCollection(node, coll, options, context);
  } else {
    node->set_err(Err("Unsupported type for new formatting system."));
  }
}

}  // namespace zxdb
