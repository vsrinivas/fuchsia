// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format.h"

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/common/function.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/expr/format_expr_value_options.h"
#include "src/developer/debug/zxdb/expr/format_node.h"
#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/expr/resolve_variant.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/member_ptr.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using NumFormat = FormatExprValueOptions::NumFormat;

// Returns true if the base type is some kind of number such that the NumFormat of the format
// options should be applied.
bool IsNumericBaseType(int base_type) {
  return base_type == BaseType::kBaseTypeSigned || base_type == BaseType::kBaseTypeUnsigned ||
         base_type == BaseType::kBaseTypeBoolean || base_type == BaseType::kBaseTypeFloat ||
         base_type == BaseType::kBaseTypeSignedChar ||
         base_type == BaseType::kBaseTypeUnsignedChar || base_type == BaseType::kBaseTypeUTF;
}

// Returns true if the given type (assumed to be a pointer) is a pointer to a function (but NOT a
// member function).
bool IsPointerToFunction(const ModifiedType* pointer) {
  FXL_DCHECK(pointer->tag() == DwarfTag::kPointerType);
  return !!pointer->modified().Get()->AsFunctionType();
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
      node->set_err(Err(
          fxl::StringPrintf("Unknown float of size %d", static_cast<int>(value.data().size()))));
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

void FormatUnsignedInt(FormatNode* node, const FormatExprValueOptions& options) {
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

// Returns true if the given symbol points to a character type that would appear in a pretty-printed
// string.
bool IsCharacterType(fxl::RefPtr<EvalContext>& eval_context, const Type* type) {
  if (!type)
    return false;
  fxl::RefPtr<Type> concrete = eval_context->GetConcreteType(type);

  // Expect a 1-byte character type.
  // TODO(brettw) handle Unicode.
  if (concrete->byte_size() != 1)
    return false;
  const BaseType* base_type = concrete->AsBaseType();
  if (!base_type)
    return false;

  return base_type->base_type() == BaseType::kBaseTypeSignedChar ||
         base_type->base_type() == BaseType::kBaseTypeUnsignedChar;
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

// Formats a numeric-style input. This assumes the type of the value in the given node has already
// been determined to be numeric. This may also be called as a fallback for things like enums.
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
void FormatZxStatusT(FormatNode* node, const FormatExprValueOptions& options) {
  FormatNumeric(node, options);

  // Caller should have checked this is the right size.
  debug_ipc::zx_status_t int_val = node->value().GetAs<debug_ipc::zx_status_t>();
  node->set_description(node->description() +
                        fxl::StringPrintf(" (%s)", debug_ipc::ZxStatusToString(int_val)));
}

void FormatEnum(FormatNode* node, const Enumeration* enum_type,
                const FormatExprValueOptions& options) {
  // Get the value out casted to a uint64.
  Err err;
  uint64_t numeric_value;
  if (enum_type->is_signed()) {
    int64_t signed_value;
    err = node->value().PromoteTo64(&signed_value);
    if (!err.has_error())
      numeric_value = static_cast<uint64_t>(signed_value);
  } else {
    err = node->value().PromoteTo64(&numeric_value);
  }
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  // When the output is marked for a specific numeric type, always skip name lookup and output the
  // numeric value below instead.
  if (options.num_format == NumFormat::kDefault) {
    const auto& map = enum_type->values();
    auto found = map.find(numeric_value);
    if (found != map.end()) {
      // Got the enum value string.
      node->set_description(found->second);
      return;
    }
    // Not found, fall through to numeric formatting.
  }

  // Invalid enum values of explicitly overridden numeric formatting gets printed as a number.
  // Be explicit about the number formatting since the enum won't be a BaseType.
  FormatExprValueOptions modified_opts = options;
  if (modified_opts.num_format == NumFormat::kDefault)
    modified_opts.num_format = enum_type->is_signed() ? NumFormat::kSigned : NumFormat::kUnsigned;
  FormatNumeric(node, modified_opts);
}

// Rust enums are formatted with the enum name in the description.
//
// The active variant will have a set of data members of which only one will be used. It will refer
// to a collection which will have the set of members. This structure will vary according to the
// type of enum:
//   EnumWithNoValue
//       The struct will have no members.
//   OneValue(u32)
//       The struct will have one member named __0
//   Tuple(u32, u32, etc.)
//       The struct will have two values, __0 and __1, etc.
//   Struct{x:u32, y:u32}
//       The struct will have "x" and "y" members.
void FormatRustEnum(FormatNode* node, const Collection* coll, const FormatExprValueOptions& options,
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

  // Add each variant data member as a child of this node. In Rust we expect exactly one but it
  // can't hurt to be general.
  std::string enum_name;
  for (const auto& lazy_member : variant->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    // Save the first member's name to be the name of the whole enum, even if there are no data
    // members. Normally there will be exactly one.
    if (enum_name.empty())
      enum_name = member->GetAssignedName();

    // TODO(brettw) this will append a child unconditionally.
    ExprValue member_value;
    err = ResolveMember(eval_context, node->value(), member, &member_value);
    if (err.has_error()) {
      // In the error case, still append a child so that the child can have
      // the error associated with it.
      node->children().push_back(std::make_unique<FormatNode>(member->GetAssignedName(), err));
    } else {
      // Only append as a child if the variant has "stuff". The case here is to skip adding children
      // for enums with no data like "Optional<Foo>::None" which will have a struct called "None"
      // with no members.
      auto member_type = member_value.GetConcreteType(eval_context.get());
      const Collection* member_coll_type = member_type->AsCollection();
      if (!member_coll_type || !member_coll_type->data_members().empty()) {
        node->children().push_back(
            std::make_unique<FormatNode>(member->GetAssignedName(), std::move(member_value)));
      }
    }
  }

  // Name for the whole node.
  node->set_description(enum_name);
}

void FormatRustTuple(FormatNode* node, const Collection* coll,
                     const FormatExprValueOptions& options, fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kRustTuple);

  // Rust tuple (and tuple struct) symbols have the tuple members encoded as "__0", "__1", etc.
  for (const auto& lazy_member : coll->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    ExprValue member_value;
    Err err = ResolveMember(eval_context, node->value(), member, &member_value);

    // Convert the names to indices "__0" -> 0.
    auto name = member->GetAssignedName();
    if (name.size() > 2 && name[0] == '_' && name[1] == '_')
      name.erase(name.begin(), name.begin() + 2);

    if (err.has_error()) {
      // In the error case, still append a child so that the child can have the error associated
      // with it.
      node->children().push_back(std::make_unique<FormatNode>(name, err));
    } else {
      node->children().push_back(std::make_unique<FormatNode>(name, member_value));
    }
  }

  // When we have a use for the short description, this should be set to: "(<0>, <1>, ...)"
}

void FormatCollection(FormatNode* node, const Collection* coll,
                      const FormatExprValueOptions& options,
                      fxl::RefPtr<EvalContext> eval_context) {
  if (coll->is_declaration()) {
    // Sometimes a value will have a type that's a forward declaration and we couldn't resolve its
    // concrete type. Print an error instead of "{}".
    node->set_err(Err("No definition."));
    return;
  }

  // Special-cases of collections.
  switch (coll->GetSpecialType()) {
    case Collection::kNotSpecial:
      break;
    case Collection::kRustEnum:
      FormatRustEnum(node, coll, options, std::move(eval_context));
      return;
    case Collection::kRustTuple:
    case Collection::kRustTupleStruct:
      FormatRustTuple(node, coll, options, std::move(eval_context));
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

    // Some base classes are empty. Only show if this base class or any of its base classes have
    // member values.
    VisitResult has_members_result = VisitClassHierarchy(from, [](const Collection* cur, uint64_t) {
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

    if (member->artificial())
      continue;  // Skip compiler-generated data.

    std::string member_name = member->GetAssignedName();

    ExprValue member_value;
    Err err = ResolveMember(eval_context, node->value(), member, &member_value);
    if (err.has_error()) {
      node->children().push_back(std::make_unique<FormatNode>(member_name, err));
    } else {
      node->children().push_back(std::make_unique<FormatNode>(member_name, member_value));
    }
  }

  node->set_description_kind(FormatNode::kCollection);
}

void FormatPointer(FormatNode* node, const FormatExprValueOptions& options,
                   fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kPointer);

  // Note: don't make assumptions about the type of value.type() since it isn't necessarily a
  // ModifiedType representing a pointer, but could be other things like a pointer to a member.

  Err err = node->value().EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  // The address goes in the description.
  node->set_description(fxl::StringPrintf("0x%" PRIx64, node->value().GetAs<TargetPointer>()));

  // Make a child node that's the dereferenced pointer value. If/when we support GUIs, we should
  // probably remove the intermediate node and put the dereferenced struct members directly as
  // children on this node. Otherwise it's an annoying extra step to expand to things.

  // Use our name but with a "*" to show it dereferenced.
  auto deref_node = std::make_unique<FormatNode>(
      "*" + node->name(),
      [ptr_value = node->value()](fxl::RefPtr<EvalContext> context,
                                  fit::callback<void(const Err& err, ExprValue value)> cb) {
        ResolvePointer(context, ptr_value, FitCallbackToStdFunction(std::move(cb)));
      });
  node->children().push_back(std::move(deref_node));
}

// For now a reference is formatted like a pointer where the outer node is the address, and the
// inner node is the "dereferenced" value. This is nice because it keeps the formatting code
// synchronous, while only the value resolution (in the child node) needs to be asynchronous.
//
// If this is put into a GUI, we'll want the reference value to be in the main description and not
// have any children. Visual Studio shows references the same as if it was a value which is probably
// the correct behavior.
void FormatReference(FormatNode* node, const FormatExprValueOptions& options,
                     fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kReference);

  Err err = node->value().EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  // The address goes in the description (see note above).
  node->set_description(fxl::StringPrintf("0x%" PRIx64, node->value().GetAs<TargetPointer>()));

  auto deref_node = std::make_unique<FormatNode>(
      std::string(),
      [ref = node->value()](fxl::RefPtr<EvalContext> context,
                            fit::callback<void(const Err& err, ExprValue value)> cb) {
        EnsureResolveReference(context, ref, FitCallbackToStdFunction(std::move(cb)));
      });
  node->children().push_back(std::move(deref_node));
}

void FormatFunctionPointer(FormatNode* node, const FormatExprValueOptions& options,
                           fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kFunctionPointer);

  Err err = node->value().EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  TargetPointer address = node->value().GetAs<TargetPointer>();
  if (address == 0) {
    // Special-case null pointers. Don't bother trying to decode the address.
    node->set_description("0x0");
    return;
  }

  // Try to symbolize the function being pointed to.
  Location loc = eval_context->GetLocationForAddress(address);
  std::string function_name;
  if (loc.symbol()) {
    if (const Function* func = loc.symbol().Get()->AsFunction())
      function_name = func->GetFullName();
  }
  if (function_name.empty()) {
    // No function name, just print out the address.
    node->set_description(fxl::StringPrintf("0x%" PRIx64, address));
  } else {
    node->set_description(fxl::StringPrintf("&%s (0x%" PRIx64 ")", function_name.c_str(), address));
  }
}

void FormatMemberPtr(FormatNode* node, const MemberPtr* type, const FormatExprValueOptions& options,
                     fxl::RefPtr<EvalContext> eval_context) {
  const Type* container_type = type->container_type().Get()->AsType();
  const Type* pointed_to_type = type->member_type().Get()->AsType();
  if (!container_type || !pointed_to_type) {
    node->set_err(Err("Missing symbol information."));
    return;
  }

  if (const FunctionType* func = pointed_to_type->AsFunctionType()) {
    // Pointers to member functions can be handled just like regular function pointers.
    FormatFunctionPointer(node, options, eval_context);
  } else {
    // Pointers to data.
    node->set_description_kind(FormatNode::kOther);
    if (Err err = node->value().EnsureSizeIs(kTargetPointerSize); err.has_error()) {
      node->set_err(err);
      return;
    }

    // The address goes in the description.
    //
    // TODO(brettw) it would be nice if this interrogated the type and figured out the name of the
    // member being pointed to. The address is not very helpful.
    node->set_description(fxl::StringPrintf("0x%" PRIx64, node->value().GetAs<TargetPointer>()));
  }
}

// Sometimes we know the real length of the array as in c "char[12]" type. In this case the expanded
// children should always include all elements, even if there is a null in the middle. This is what
// length_was_known means. When unset we assume a guessed length (as in "char*"), stop at the first
// null, and don't include it.
//
// TODO(brettw) currently this handles 8-bit chracters only.
void FormatCharArray(FormatNode* node, fxl::RefPtr<Type> char_type, const uint8_t* data,
                     size_t length, bool length_was_known, bool truncated) {
  node->set_description_kind(FormatNode::kString);

  // Expect the string to be null-terminated. If we didn't find a null before the end of the buffer,
  // mark as truncated.
  size_t output_len = strnlen(reinterpret_cast<const char*>(data), length);

  // It's possible a null happened before the end of the buffer, in which case it's no longer
  // truncated.
  if (output_len < length)
    truncated = false;

  // Generate the string in the desciption. Stop at the first null (computed above) and don't
  // include it.
  std::string result("\"");
  for (size_t i = 0; i < output_len; i++)
    AppendEscapedChar(data[i], &result);
  result.push_back('"');

  // Add children to the first null unless the length was known in advance.
  size_t child_len = length_was_known ? length : output_len;
  for (size_t i = 0; i < child_len; i++) {
    node->children().push_back(std::make_unique<FormatNode>(fxl::StringPrintf("[%zu]", i),
                                                            ExprValue(char_type, {data[i]})));
  }

  // Add an indication if the string was truncated to the max size.
  if (truncated) {
    result += "...";
    node->children().push_back(std::make_unique<FormatNode>("..."));
  }

  node->set_description(result);
}

void FormatCharPointer(FormatNode* node, const Type* char_type,
                       const FormatExprValueOptions& options, fxl::RefPtr<EvalContext> eval_context,
                       fit::deferred_callback cb) {
  node->set_description_kind(FormatNode::kString);

  if (node->value().data().size() != kTargetPointerSize) {
    node->set_err(Err("Bad pointer data."));
    return;
  }

  TargetPointer address = node->value().GetAs<TargetPointer>();
  if (!address) {
    // Special-case null pointers to just print a null address.
    node->set_description("0x0");
    return;
  }

  // Speculatively request the max string size.
  uint32_t bytes_to_fetch = options.max_array_size;
  if (bytes_to_fetch == 0) {
    // No array data should be fetched. Indicate that the result was truncated.
    node->set_description("\"\"...");
    return;
  }

  fxl::RefPtr<SymbolDataProvider> data_provider = eval_context->GetDataProvider();

  // TODO(brettw) When GetMemoryAsync takes a move-only fit::callback the cb can just be
  // std::move-ed into the lambda.
  auto shared_cb = std::make_shared<fit::deferred_callback>(std::move(cb));
  data_provider->GetMemoryAsync(
      address, bytes_to_fetch,
      [address, bytes_to_fetch, char_type = RefPtrTo(char_type), weak_node = node->GetWeakPtr(),
       shared_cb](const Err& err, std::vector<uint8_t> data) {
        if (!weak_node)
          return;

        if (data.empty()) {
          // Should not have requested 0 size, so it if came back empty the pointer was invalid.
          weak_node->set_err(Err("0x%" PRIx64 " «invalid pointer»", address));
          return;
        }

        // Report as truncated because if the string goes to the end of this array it will be.
        // FormatCharArray will clear this flag if it finds a null before the end of the buffer.
        //
        // Don't want to set truncated if the data ended before the requested size, this means it
        // hit the end of valid memory, so we're not omitting data by only showing that part of it.
        bool truncated = data.size() == bytes_to_fetch;
        FormatCharArray(weak_node.get(), char_type, &data[0], data.size(), false, truncated);
      });
}

// Formats an array with a known length. This is for non-char arrays (which are special-cased in
// FormatCharArray).
void FormatArray(FormatNode* node, int elt_count, const FormatExprValueOptions& options,
                 fxl::RefPtr<EvalContext> eval_context) {
  node->set_description_kind(FormatNode::kArray);

  // Arrays should have known non-zero sizes.
  FXL_DCHECK(elt_count >= 0);
  int print_count = std::min(static_cast<int>(options.max_array_size), elt_count);

  std::vector<ExprValue> items;
  Err err = ResolveArray(eval_context, node->value(), 0, print_count, &items);
  if (err.has_error()) {
    node->set_err(err);
    return;
  }

  for (size_t i = 0; i < items.size(); i++) {
    node->children().push_back(
        std::make_unique<FormatNode>(fxl::StringPrintf("[%zu]", i), std::move(items[i])));
  }

  if (static_cast<uint32_t>(elt_count) > items.size()) {
    // Add "..." annotation to show some things were clipped.
    node->children().push_back(std::make_unique<FormatNode>("..."));
  }
}

// Attempts to format arrays, char arrays, and char pointers. Because these are many different types
// this is handled by a separate helper function.
//
// Returns true if the node was formatted by this function. If the operation is asynchronous the
// callback will be moved from to defer it until the async operation is complete.
//
// A false return value means this was not an array or a string and other types of formatting should
// be attempted. The callback will be unmodified.
bool TryFormatArrayOrString(FormatNode* node, const Type* type,
                            const FormatExprValueOptions& options,
                            fxl::RefPtr<EvalContext> eval_context, fit::deferred_callback& cb) {
  FXL_DCHECK(type == type->StripCVT());

  if (type->tag() == DwarfTag::kPointerType) {
    // Any pointer type (we only char about char*).
    const ModifiedType* modified = type->AsModifiedType();
    if (!modified)
      return false;

    const Type* char_type = modified->modified().Get()->AsType();
    if (IsCharacterType(eval_context, char_type)) {
      FormatCharPointer(node, char_type, options, eval_context, std::move(cb));
      return true;
    }
    return false;  // All other pointer types are unhandled.
  } else if (type->tag() == DwarfTag::kArrayType) {
    // Any array type with a known size (we care about both).
    const ArrayType* array = type->AsArrayType();
    if (!array)
      return false;

    auto value_type = eval_context->GetConcreteType(array->value_type());
    if (!value_type)
      return false;

    if (IsCharacterType(eval_context, value_type.get())) {
      size_t length = array->num_elts();
      bool truncated = false;
      if (length > options.max_array_size) {
        length = options.max_array_size;
        truncated = true;
      }
      FormatCharArray(node, value_type, node->value().data().data(), length, true, truncated);
    } else {
      FormatArray(node, array->num_elts(), options, eval_context);
    }
    return true;
  }
  return false;
}

// Unspecified types are normally nullptr_t and print as a number (probably 0x0).
void FormatUnspecified(FormatNode* node) {
  node->set_description_kind(FormatNode::kOther);

  uint64_t unspecified_value = 0;
  if (node->value().PromoteTo64(&unspecified_value).has_error())
    node->set_description("<unspecified>");
  else
    node->set_description(fxl::StringPrintf("0x%" PRIx64, unspecified_value));
}

}  // namespace

void FillFormatNodeValue(FormatNode* node, fxl::RefPtr<EvalContext> context,
                         fit::deferred_callback cb) {
  switch (node->source()) {
    case FormatNode::kValue:
      // Already has the value.
      return;
    case FormatNode::kExpression: {
      // Evaluate the expression.
      // TODO(brettw) remove this make_shared when EvalExpression takes a fit::callback.
      auto shared_cb = std::make_shared<fit::deferred_callback>(std::move(cb));
      EvalExpression(node->expression(), context, true,
                     [weak_node = node->GetWeakPtr(), shared_cb](const Err& err, ExprValue value) {
                       if (!weak_node)
                         return;
                       if (err.has_error()) {
                         weak_node->set_err(err);
                         weak_node->SetValue(ExprValue());
                       } else {
                         weak_node->SetValue(std::move(value));
                       }
                     });
      return;
    }
    case FormatNode::kProgramatic:
      // Lambda provides the value.
      node->FillProgramaticValue(std::move(context), std::move(cb));
      return;
  }
  FXL_NOTREACHED();
}

void FillFormatNodeDescription(FormatNode* node, const FormatExprValueOptions& options,
                               fxl::RefPtr<EvalContext> context, fit::deferred_callback cb) {
  if (node->state() == FormatNode::kEmpty || node->state() == FormatNode::kUnevaluated ||
      node->err().has_error()) {
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

  // Special-case zx_status_t. Long-term this should be removed and replaced with a pretty-printing
  // system where this can be expressed generically.
  //
  // This code needs to go here because zx_status_t is a typedef that will be expanded away by the
  // GetConcreteType() below.
  if (node->value().type()->GetFullName() == "zx_status_t" &&
      node->value().type()->byte_size() == sizeof(debug_ipc::zx_status_t)) {
    FormatZxStatusT(node, options);
    return;
  }

  // Trim "const", "volatile", etc. and follow typedef and using for the type checking below.
  //
  // Always use this variable below instead of value.type().
  fxl::RefPtr<Type> type = node->value().GetConcreteType(context.get());

  // Arrays and strings.
  if (TryFormatArrayOrString(node, type.get(), options, context, cb))
    return;

  if (const ModifiedType* modified_type = type->AsModifiedType()) {
    // Modified types (references were handled above).
    switch (modified_type->tag()) {
      case DwarfTag::kPointerType:
        // Function pointers need special handling.
        if (IsPointerToFunction(modified_type))
          FormatFunctionPointer(node, options, context);
        else
          FormatPointer(node, options, context);
        break;
      case DwarfTag::kReferenceType:
      case DwarfTag::kRvalueReferenceType:
        FormatReference(node, options, context);
        break;
      default:
        node->set_err(Err("Unhandled type modifier 0x%x, please file a bug.",
                          static_cast<unsigned>(modified_type->tag())));
        break;
    }
  } else if (IsNumericBaseType(node->value().GetBaseType())) {
    // Numeric types.
    FormatNumeric(node, options);
  } else if (const MemberPtr* member_ptr = type->AsMemberPtr()) {
    // Pointers to class/struct members.
    FormatMemberPtr(node, member_ptr, options, context);
  } else if (const FunctionType* func = type->AsFunctionType()) {
    // Functions. These don't have a direct C++ equivalent without being
    // modified by a "pointer". Assume these act like pointers to functions.
    FormatFunctionPointer(node, options, context);
  } else if (const Enumeration* enum_type = type->AsEnumeration()) {
    // Enumerations.
    FormatEnum(node, enum_type, options);
  } else if (const Collection* coll = type->AsCollection()) {
    // Collections (structs, classes, and unions).
    FormatCollection(node, coll, options, context);
  } else if (type->tag() == DwarfTag::kUnspecifiedType) {
    // Unspecified (nullptr_t).
    FormatUnspecified(node);
  } else {
    node->set_err(Err("Unsupported type for new formatting system."));
  }
}

}  // namespace zxdb
