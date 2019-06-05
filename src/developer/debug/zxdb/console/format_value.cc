// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_value.h"

#include <ctype.h>
#include <string.h>

#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/expr/expr_eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/resolve_array.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/enumeration.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/member_ptr.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using NumFormat = FormatExprValueOptions::NumFormat;
using Verbosity = FormatExprValueOptions::Verbosity;

// When there are errors during value printing we can't just print them since
// they're associated with a value. This function formats the error in a way
// appropriate for value output.
OutputBuffer ErrToOutput(const Err& err) {
  FXL_DCHECK(err.has_error());
  return OutputBuffer(Syntax::kComment, "<" + err.msg() + ">");
}

OutputBuffer ErrStringToOutput(const std::string& s) {
  return OutputBuffer(Syntax::kComment, "<" + s + ">");
}

OutputBuffer InvalidPointerToOutput(TargetPointer address) {
  OutputBuffer out;
  out.Append(OutputBuffer(fxl::StringPrintf("0x%" PRIx64 " ", address)));
  out.Append(ErrStringToOutput("invalid pointer"));
  return out;
}

// Get a possibly-elided version of the type name for a medium verbosity level.
std::string GetElidedTypeName(const std::string& name) {
  // Names shorter than this are always displayed in full.
  if (name.size() <= 32)
    return name;

  // This value was picked to be smaller than the above value so we don't elide
  // one or two characters (which looks dumb). It was selected to be long
  // enough so that with the common prefix of "std::__2::" (which occurs on
  // many long types), you still get enough to read approximately what the
  // type is.
  return name.substr(0, 20) + "…";
}

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

// Returns true if the given symbol points to a character type that would
// appear in a pretty-printed string.
bool IsCharacterType(fxl::RefPtr<ExprEvalContext>& eval_context,
                     const Type* type) {
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
bool IsCharacterType(fxl::RefPtr<ExprEvalContext>& eval_context,
                     const LazySymbol& symbol) {
  return IsCharacterType(eval_context, symbol.Get()->AsType());
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

// Returns true if the given type (assumed to be a pointer) is a pointer to
// a function (but NOT a member function).
bool IsPointerToFunction(const ModifiedType* pointer) {
  FXL_DCHECK(pointer->tag() == DwarfTag::kPointerType);
  return !!pointer->modified().Get()->AsFunctionType();
}

}  // namespace

FormatValue::FormatValue(std::unique_ptr<ProcessContext> process_context)
    : process_context_(std::move(process_context)), weak_factory_(this) {}
FormatValue::~FormatValue() = default;

void FormatValue::AppendValue(fxl::RefPtr<ExprEvalContext> eval_context,
                              const ExprValue& value,
                              const FormatExprValueOptions& options) {
  FormatExprValue(eval_context, value, options, false,
                  AsyncAppend(GetRootOutputKey()));
}

void FormatValue::AppendVariable(const SymbolContext& symbol_context,
                                 fxl::RefPtr<ExprEvalContext> eval_context,
                                 const Variable* var,
                                 const FormatExprValueOptions& options) {
  OutputKey output_key = AsyncAppend(
      NodeType::kVariable, var->GetAssignedName(), GetRootOutputKey());

  eval_context->GetVariableValue(
      fxl::RefPtr<Variable>(const_cast<Variable*>(var)),
      [weak_this = weak_factory_.GetWeakPtr(), eval_context, options,
       output_key](const Err& err, fxl::RefPtr<Symbol>, ExprValue val) {
        // The variable has been resolved, now we need to print it (which could
        // in itself be asynchronous).
        if (weak_this) {
          weak_this->FormatExprValue(eval_context, err, val, options, false,
                                     output_key);
        }
      });
}

void FormatValue::Append(OutputBuffer out) {
  AppendToOutputKey(GetRootOutputKey(), std::move(out));
}

void FormatValue::Append(std::string str) {
  Append(OutputBuffer(std::move(str)));
}

void FormatValue::Complete(Callback callback) {
  FXL_DCHECK(!complete_callback_);
  complete_callback_ = std::move(callback);

  // If there are no pending formats, issue the callback right away.
  CheckPendingResolution();
  // WARNING: |this| may be deleted.
}

void FormatValue::FormatExprValue(fxl::RefPtr<ExprEvalContext> eval_context,
                                  const ExprValue& value,
                                  const FormatExprValueOptions& options,
                                  bool suppress_type_printing,
                                  OutputKey output_key) {
  if (!value.type()) {
    OutputKeyComplete(output_key, ErrStringToOutput("no type"));
    return;
  }

  // First output the type if required. Do this before stripping C-V
  // qualifications so the printed name is the original.
  if (options.verbosity == Verbosity::kAllTypes && !suppress_type_printing) {
    AppendToOutputKey(
        output_key,
        OutputBuffer(
            Syntax::kComment,
            fxl::StringPrintf("(%s) ", value.type()->GetFullName().c_str())));
  }

  // Special-case zx_status_t. Long-term this should be removed and replaced
  // with a pretty-printing system where this can be expressed generically.
  // This code needs to go here because zx_status_t is a typedef that will be
  // expanded away by GetConcreteType().
  if (value.type()->GetFullName() == "zx_status_t" &&
      value.type()->byte_size() == sizeof(debug_ipc::zx_status_t)) {
    FormatZxStatusT(value, options, output_key);
    return;
  }

  // Trim "const", "volatile", etc. and follow typedef and using for the type
  // checking below.
  //
  // Always use this variable below instead of value.type().
  fxl::RefPtr<Type> type = value.GetConcreteType(eval_context.get());

  // Structs and classes.
  if (const Collection* coll = type->AsCollection()) {
    FormatCollection(eval_context, coll, value, options, output_key);
    return;
  }

  // Arrays and strings.
  if (TryFormatArrayOrString(eval_context, type.get(), value, options,
                             output_key))
    return;

  // References (these require asynchronous calls to format so can't be in the
  // "modified types" block below in the synchronous section).
  if (type->tag() == DwarfTag::kReferenceType ||
      type->tag() == DwarfTag::kRvalueReferenceType) {
    FormatReference(eval_context, value, options, output_key);
    return;
  }

  // Everything below here is formatted synchronously. Do not early return
  // since the bottom of this function sets the output and marks the output key
  // resolved.
  OutputBuffer out;

  if (const ModifiedType* modified_type = type->AsModifiedType()) {
    // Modified types (references were handled above).
    switch (modified_type->tag()) {
      case DwarfTag::kPointerType:
        // Function pointers need special handling.
        if (IsPointerToFunction(modified_type))
          FormatFunctionPointer(value, options, &out);
        else
          FormatPointer(value, options, &out);
        break;
      default:
        out.Append(Syntax::kComment,
                   fxl::StringPrintf(
                       "<Unhandled type modifier 0x%x, please file a bug.>",
                       static_cast<unsigned>(modified_type->tag())));
        break;
    }
  } else if (const MemberPtr* member_ptr = type->AsMemberPtr()) {
    // Pointers to class/struct members.
    FormatMemberPtr(value, member_ptr, options, &out);
  } else if (const FunctionType* func = type->AsFunctionType()) {
    // Functions. These don't have a direct C++ equivalent without being
    // modified by a "pointer". Assume these act like pointers to functions.
    FormatFunctionPointer(value, options, &out);
  } else if (const Enumeration* enum_type = type->AsEnumeration()) {
    // Enumerations.
    FormatEnum(value, enum_type, options, &out);
  } else if (IsNumericBaseType(value.GetBaseType())) {
    // Numeric types.
    FormatNumeric(value, options, &out);
  } else if (type->tag() == DwarfTag::kUnspecifiedType) {
    // Unspecified, assume nullptr_t and print as a number (probably 0x0).
    uint64_t unspecified_value = 0;
    if (value.PromoteTo64(&unspecified_value).has_error())
      out.Append("<unspecified>");
    else
      out.Append(fxl::StringPrintf("0x%" PRIx64, unspecified_value));
  } else {
    // Non-numeric base types.
    switch (value.GetBaseType()) {
      case BaseType::kBaseTypeAddress: {
        // Always print addresses as unsigned hex.
        FormatExprValueOptions overridden(options);
        overridden.num_format = NumFormat::kHex;
        FormatUnsignedInt(value, options, &out);
        break;
      }
      case BaseType::kBaseTypeNone: {
        // Void. Just print the type name with no data.
        out.Append(type->GetFullName());
        break;
      }
      default:
        if (value.data().empty()) {
          out.Append(ErrStringToOutput("no data"));
        } else {
          // For now, print a hex dump for everything else.
          std::string result;
          for (size_t i = 0; i < value.data().size(); i++) {
            if (i > 0)
              result.push_back(' ');
            result.append(fxl::StringPrintf("0x%02x", value.data()[i]));
          }
          out.Append(std::move(result));
        }
    }
  }
  OutputKeyComplete(output_key, std::move(out));
}

void FormatValue::FormatExprValue(fxl::RefPtr<ExprEvalContext> eval_context,
                                  const Err& err, const ExprValue& value,
                                  const FormatExprValueOptions& options,
                                  bool suppress_type_printing,
                                  OutputKey output_key) {
  if (err.has_error()) {
    // If the future we probably want to rewrite "optimized out" errors to
    // something shorter. The evaluator makes a longer message suitable for
    // printing to the console in response to a command, but is too long
    // for printing as as the value in "foo = bar". For now, though, the longer
    // messages can be helpful for debugging. It would be:
    //   if (err.type() == ErrType::kOptimizedOut)
    //      out->Append(ErrStringToOutput("optimized out"));
    OutputKeyComplete(output_key, ErrToOutput(err));
  } else {
    FormatExprValue(std::move(eval_context), value, options,
                    suppress_type_printing, output_key);
  }
}

// GDB format:
//   {<BaseClass> = { ... }, a = 1, b = 2, sub_struct = {foo = 1, bar = 2}}
//
// LLDB format:
//   {
//     BaseClass = { ... }
//     a = 1
//     b = 2
//     sub_struct = {
//       foo = 1
//       bar = 2
//     }
//   }
void FormatValue::FormatCollection(fxl::RefPtr<ExprEvalContext> eval_context,
                                   const Collection* coll,
                                   const ExprValue& value,
                                   const FormatExprValueOptions& options,
                                   OutputKey output_key) {
  if (coll->is_declaration()) {
    // Sometimes a value will have a type that's a forward declaration and we
    // couldn't resolve its concrete type. Print an error instead of "{}".
    OutputKeyComplete(
        output_key, OutputBuffer(Syntax::kComment, "<No definition>"));
    return;
  }

  AppendToOutputKey(output_key, OutputBuffer("{"));

  // True after printing the first item.
  bool needs_comma = false;

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

    if (needs_comma)
      AppendToOutputKey(output_key, OutputBuffer(", "));
    else
      needs_comma = true;

    // Print "ClassName = "
    std::string base_name = from->GetFullName();
    if (options.verbosity == Verbosity::kMinimal)
      base_name = GetElidedTypeName(base_name);

    // Pass "true" to suppress type printing since we just printed the type.
    ExprValue from_value;
    Err err = ResolveInherited(value, inherited, &from_value);
    FormatExprValue(
        eval_context, err, from_value, options, true,
        AsyncAppend(NodeType::kBaseClass, std::move(base_name), output_key));
  }

  // Data members.
  for (const auto& lazy_member : coll->data_members()) {
    const DataMember* member = lazy_member.Get()->AsDataMember();
    if (!member)
      continue;

    if (needs_comma)
      AppendToOutputKey(output_key, OutputBuffer(", "));
    else
      needs_comma = true;

    ExprValue member_value;
    Err err = ResolveMember(eval_context, value, member, &member_value);

    // Type info if requested.
    if (options.verbosity == Verbosity::kAllTypes && member_value.type()) {
      AppendToOutputKey(
          output_key,
          OutputBuffer(
              Syntax::kComment,
              fxl::StringPrintf("(%s) ",
                                member_value.type()->GetFullName().c_str())));
    }

    // Force omitting the type info since we already handled that before
    // showing the name. This is because:
    //   (int) b = 12
    // looks better than:
    //   b = (int) 12
    FormatExprValue(eval_context, err, member_value, options, true,
                    AsyncAppend(NodeType::kVariable, member->GetAssignedName(),
                                output_key));
  }
  AppendToOutputKey(output_key, OutputBuffer("}"));
  OutputKeyComplete(output_key);
}

bool FormatValue::TryFormatArrayOrString(
    fxl::RefPtr<ExprEvalContext> eval_context, const Type* type,
    const ExprValue& value, const FormatExprValueOptions& options,
    OutputKey output_key) {
  FXL_DCHECK(type == type->StripCVT());

  if (type->tag() == DwarfTag::kPointerType) {
    // Any pointer type (we only char about char*).
    const ModifiedType* modified = type->AsModifiedType();
    if (!modified)
      return false;

    if (IsCharacterType(eval_context, modified->modified())) {
      FormatCharPointer(eval_context, type, value, options, output_key);
      return true;
    }
    return false;  // All other pointer types are unhandled.
  } else if (type->tag() == DwarfTag::kArrayType) {
    // Any array type with a known size (we care about both).
    const ArrayType* array = type->AsArrayType();
    if (!array)
      return false;

    if (IsCharacterType(eval_context, array->value_type())) {
      size_t length = array->num_elts();
      bool truncated = false;
      if (length > options.max_array_size) {
        length = options.max_array_size;
        truncated = true;
      }
      FormatCharArray(value.data().data(), length, truncated, output_key);
    } else {
      FormatArray(eval_context, value, array->num_elts(), options, output_key);
    }
    return true;
  }
  return false;
}

void FormatValue::FormatCharPointer(fxl::RefPtr<ExprEvalContext> eval_context,
                                    const Type* type, const ExprValue& value,
                                    const FormatExprValueOptions& options,
                                    OutputKey output_key) {
  if (value.data().size() != kTargetPointerSize) {
    OutputKeyComplete(output_key, ErrStringToOutput("Bad pointer data."));
    return;
  }

  TargetPointer address = value.GetAs<TargetPointer>();
  if (!address) {
    // Special-case null pointers to just print a null address.
    OutputKeyComplete(output_key, OutputBuffer("0x0"));
    return;
  }

  // Speculatively request the max string size.
  uint32_t bytes_to_fetch = options.max_array_size;
  if (bytes_to_fetch == 0) {
    // No array data should be fetched. Indicate that the result was truncated.
    OutputKeyComplete(output_key, OutputBuffer("\"\"..."));
    return;
  }

  fxl::RefPtr<SymbolDataProvider> data_provider =
      eval_context->GetDataProvider();
  data_provider->GetMemoryAsync(
      address, bytes_to_fetch,
      [address, bytes_to_fetch, weak_this = weak_factory_.GetWeakPtr(),
       output_key](const Err& err, std::vector<uint8_t> data) {
        if (!weak_this)
          return;

        if (data.empty()) {
          // Should not have requested 0 size, so it if came back empty the
          // pointer was invalid.
          weak_this->OutputKeyComplete(output_key,
                                       InvalidPointerToOutput(address));
          return;
        }

        // Report as truncated because if the string goes to the end of this
        // array it will be. FormatCharArray will clear this flag if it finds a
        // null before the end of the buffer.
        //
        // Don't want to set truncated if the data ended before the requested
        // size, this means it hit the end of valid memory, so we're not
        // omitting data by only showing that part of it.
        bool truncated = data.size() == bytes_to_fetch;
        weak_this->FormatCharArray(&data[0], data.size(), truncated,
                                   output_key);
      });
}

void FormatValue::FormatCharArray(const uint8_t* data, size_t length,
                                  bool truncated, OutputKey output_key) {
  // Expect the string to be null-terminated. If we didn't find a null before
  // the end of the buffer, mark as truncated.
  size_t output_len = strnlen(reinterpret_cast<const char*>(data), length);

  // It's possible a null happened before the end of the buffer, in which
  // case it's no longer truncated.
  if (output_len < length)
    truncated = false;

  std::string result("\"");
  for (size_t i = 0; i < output_len; i++)
    AppendEscapedChar(data[i], &result);
  result.push_back('"');

  // Add an indication if the string was truncated to the max size.
  if (truncated)
    result += "...";

  OutputKeyComplete(output_key, OutputBuffer(result));
}

void FormatValue::FormatArray(fxl::RefPtr<ExprEvalContext> eval_context,
                              const ExprValue& value, int elt_count,
                              const FormatExprValueOptions& options,
                              OutputKey output_key) {
  // Arrays should have known non-zero sizes.
  FXL_DCHECK(elt_count >= 0);
  int print_count =
      std::min(static_cast<int>(options.max_array_size), elt_count);

  std::vector<ExprValue> items;
  Err err = ResolveArray(eval_context, value, 0, print_count, &items);
  if (err.has_error()) {
    OutputKeyComplete(output_key, ErrToOutput(err));
    return;
  }

  AppendToOutputKey(output_key, OutputBuffer("{"));

  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0)
      AppendToOutputKey(output_key, OutputBuffer(", "));

    // Avoid forcing type info for every array value. This will be encoded in
    // the main array type.
    FormatExprValue(eval_context, items[i], options, true,
                    AsyncAppend(output_key));
  }

  AppendToOutputKey(
      output_key,
      OutputBuffer(static_cast<uint32_t>(elt_count) > items.size() ? ", ...}"
                                                                   : "}"));

  // Now we can mark the root output key as complete. The children added above
  // may or may not have completed synchronously.
  OutputKeyComplete(output_key);
}

void FormatValue::FormatNumeric(const ExprValue& value,
                                const FormatExprValueOptions& options,
                                OutputBuffer* out) {
  if (options.num_format != NumFormat::kDefault) {
    // Overridden format option.
    switch (options.num_format) {
      case NumFormat::kUnsigned:
      case NumFormat::kHex:
        FormatUnsignedInt(value, options, out);
        break;
      case NumFormat::kSigned:
        FormatSignedInt(value, out);
        break;
      case NumFormat::kChar:
        FormatChar(value, out);
        break;
      case NumFormat::kDefault:
        // Prevent warning for unused enum type.
        break;
    }
  } else {
    // Default handling for base types based on the number.
    switch (value.GetBaseType()) {
      case BaseType::kBaseTypeBoolean:
        FormatBoolean(value, out);
        break;
      case BaseType::kBaseTypeFloat:
        FormatFloat(value, out);
        break;
      case BaseType::kBaseTypeSigned:
        FormatSignedInt(value, out);
        break;
      case BaseType::kBaseTypeUnsigned:
        FormatUnsignedInt(value, options, out);
        break;
      case BaseType::kBaseTypeSignedChar:
      case BaseType::kBaseTypeUnsignedChar:
      case BaseType::kBaseTypeUTF:
        FormatChar(value, out);
        break;
    }
  }
}

void FormatValue::FormatBoolean(const ExprValue& value, OutputBuffer* out) {
  uint64_t int_val = 0;
  Err err = value.PromoteTo64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else if (int_val)
    out->Append("true");
  else
    out->Append("false");
}

void FormatValue::FormatEnum(const ExprValue& value,
                             const Enumeration* enum_type,
                             const FormatExprValueOptions& options,
                             OutputBuffer* out) {
  // Get the value out casted to a uint64.
  Err err;
  uint64_t numeric_value;
  if (enum_type->is_signed()) {
    int64_t signed_value;
    err = value.PromoteTo64(&signed_value);
    if (!err.has_error())
      numeric_value = static_cast<uint64_t>(signed_value);
  } else {
    err = value.PromoteTo64(&numeric_value);
  }
  if (err.has_error()) {
    out->Append(ErrToOutput(err));
    return;
  }

  // When the output is marked for a specific numeric type, always skip name
  // lookup and output the numeric value below instead.
  if (options.num_format == NumFormat::kDefault) {
    const auto& map = enum_type->values();
    auto found = map.find(numeric_value);
    if (found != map.end()) {
      // Got the enum value string.
      out->Append(found->second);
      return;
    }
    // Not found, fall through to numeric output.
  }

  // Invalid enum values of explicitly overridden numeric formatting gets
  // printed as a number.
  FormatExprValueOptions modified_opts = options;
  if (modified_opts.num_format == NumFormat::kDefault) {
    // Compute the formatting for invalid enum values when there is no numeric
    // override.
    modified_opts.num_format =
        enum_type->is_signed() ? NumFormat::kSigned : NumFormat::kUnsigned;
  }
  FormatNumeric(value, modified_opts, out);
}

void FormatValue::FormatFloat(const ExprValue& value, OutputBuffer* out) {
  switch (value.data().size()) {
    case sizeof(float):
      out->Append(fxl::StringPrintf("%g", value.GetAs<float>()));
      break;
    case sizeof(double):
      out->Append(fxl::StringPrintf("%g", value.GetAs<double>()));
      break;
    default:
      out->Append(ErrStringToOutput(fxl::StringPrintf(
          "unknown float of size %d", static_cast<int>(value.data().size()))));
      break;
  }
}

void FormatValue::FormatSignedInt(const ExprValue& value, OutputBuffer* out) {
  int64_t int_val = 0;
  Err err = value.PromoteTo64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else
    out->Append(fxl::StringPrintf("%" PRId64, int_val));
}

void FormatValue::FormatUnsignedInt(const ExprValue& value,
                                    const FormatExprValueOptions& options,
                                    OutputBuffer* out) {
  // This formatter handles unsigned and hex output.
  uint64_t int_val = 0;
  Err err = value.PromoteTo64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else if (options.num_format == NumFormat::kHex)
    out->Append(fxl::StringPrintf("0x%" PRIx64, int_val));
  else
    out->Append(fxl::StringPrintf("%" PRIu64, int_val));
}

void FormatValue::FormatChar(const ExprValue& value, OutputBuffer* out) {
  // Just take the first byte for all char.
  if (value.data().empty()) {
    out->Append(ErrStringToOutput("invalid char type"));
    return;
  }
  std::string str;
  str.push_back('\'');
  AppendEscapedChar(value.data()[0], &str);
  str.push_back('\'');
  out->Append(str);
}

void FormatValue::FormatPointer(const ExprValue& value,
                                const FormatExprValueOptions& options,
                                OutputBuffer* out) {
  // Don't make assumptions about the type of value.type() since it isn't
  // necessarily a ModifiedType representing a pointer, but could be other
  // things like a pointer to a member.

  // Type info. The caller will have handled the case when type printing was
  // forced always on, so we need only handle the lower verbosities.
  if (options.verbosity == Verbosity::kMinimal) {
    out->Append(Syntax::kComment, "(*) ");
  } else if (options.verbosity == Verbosity::kMedium) {
    out->Append(
        Syntax::kComment,
        fxl::StringPrintf("(%s) ", value.type()->GetFullName().c_str()));
  }

  Err err = value.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else
    out->Append(fxl::StringPrintf("0x%" PRIx64, value.GetAs<TargetPointer>()));
}

void FormatValue::FormatReference(fxl::RefPtr<ExprEvalContext> eval_context,
                                  const ExprValue& value,
                                  const FormatExprValueOptions& options,
                                  OutputKey output_key) {
  EnsureResolveReference(
      eval_context, value,
      [weak_this = weak_factory_.GetWeakPtr(), eval_context,
       original_value = value, options,
       output_key](const Err& err, ExprValue resolved_value) {
        if (!weak_this)
          return;

        OutputBuffer out;

        // First show the type if desired. As with pointers, the calling code
        // will have printed the type for the "all types" case.
        if (options.verbosity == Verbosity::kMedium) {
          out.Append(Syntax::kComment,
                     fxl::StringPrintf(
                         "(%s) ",
                         GetElidedTypeName(original_value.type()->GetFullName())
                             .c_str()));
        }

        // Followed by the address (only in non-minimal modes).
        if (options.verbosity != Verbosity::kMinimal) {
          TargetPointer address = 0;
          Err addr_err = original_value.PromoteTo64(&address);
          if (addr_err.has_error()) {
            // Invalid data in the reference.
            out.Append(ErrToOutput(addr_err));
            weak_this->OutputKeyComplete(output_key, std::move(out));
            return;
          }
          out.Append(Syntax::kComment,
                     fxl::StringPrintf("0x%" PRIx64 " = ", address));
        }

        // Follow with the resolved value.
        if (err.has_error()) {
          out.Append(ErrToOutput(err));
          weak_this->OutputKeyComplete(output_key, std::move(out));
        } else {
          // FormatExprValue will mark the output key complete when it's done
          // formatting. Pass true for suppress_type_printing since the type of
          // the reference was printed above.
          weak_this->AppendToOutputKey(output_key, std::move(out));
          weak_this->FormatExprValue(eval_context, resolved_value, options,
                                     true, output_key);
        }
      });
}

void FormatValue::FormatFunctionPointer(const ExprValue& value,
                                        const FormatExprValueOptions& options,
                                        OutputBuffer* out) {
  // Unlike pointers, we don't print the type for function pointers. These
  // are usually very long and not very informative. If explicitly requested,
  // the types will be printed out by the calling function.

  Err err = value.EnsureSizeIs(kTargetPointerSize);
  if (err.has_error()) {
    out->Append(ErrToOutput(err));
    return;
  }

  // Allow overrides for the number format. Normally one would expect to
  // provide a hex override to get the address rather than the resolved
  // function name.
  if (options.num_format != NumFormat::kDefault) {
    FormatNumeric(value, options, out);
    return;
  }

  TargetPointer address = value.GetAs<TargetPointer>();
  if (address == 0) {
    // Special-case null pointers. Don't bother trying to decode the address
    // or show a type.
    out->Append("0x0");
    return;
  }

  // Try to symbolize the function being pointed to.
  Location loc = process_context_->GetLocationForAddress(address);
  std::string function_name;
  if (loc.symbol()) {
    if (const Function* func = loc.symbol().Get()->AsFunction())
      function_name = func->GetFullName();
  }
  if (function_name.empty()) {
    // No function name, just print out the address.
    out->Append(fxl::StringPrintf("0x%" PRIx64, address));
  } else {
    out->Append("&" + function_name);
  }
}

void FormatValue::FormatMemberPtr(const ExprValue& value, const MemberPtr* type,
                                  const FormatExprValueOptions& options,
                                  OutputBuffer* out) {
  const Type* container_type = type->container_type().Get()->AsType();
  const Type* pointed_to_type = type->member_type().Get()->AsType();
  if (!container_type || !pointed_to_type) {
    out->Append("<missing symbol information>");
    return;
  }

  if (const FunctionType* func = pointed_to_type->AsFunctionType()) {
    // Pointers to member functions can be handled just like regular function
    // pointers.
    FormatFunctionPointer(value, options, out);
  } else {
    // Pointers to everything else can be handled like normal pointers.
    FormatPointer(value, options, out);
  }
}

void FormatValue::FormatZxStatusT(const ExprValue& value,
                                  const FormatExprValueOptions& options,
                                  OutputKey output_key) {
  OutputBuffer out;
  FormatNumeric(value, options, &out);

  // Caller should have checked this is the right size.
  debug_ipc::zx_status_t int_val = value.GetAs<debug_ipc::zx_status_t>();
  out.Append(Syntax::kComment,
             fxl::StringPrintf(" (%s)", debug_ipc::ZxStatusToString(int_val)));
  OutputKeyComplete(output_key, std::move(out));
}

FormatValue::OutputKey FormatValue::GetRootOutputKey() {
  return reinterpret_cast<intptr_t>(&root_);
}

void FormatValue::AppendToOutputKey(OutputKey output_key, OutputBuffer buffer) {
  // See OutputKey definition in the header for how it works.
  OutputNode* parent_node = reinterpret_cast<OutputNode*>(output_key);
  auto new_node = std::make_unique<OutputNode>();
  new_node->buffer = std::move(buffer);
  parent_node->child.push_back(std::move(new_node));
}

FormatValue::OutputKey FormatValue::AsyncAppend(OutputKey parent) {
  return AsyncAppend(NodeType::kGeneric, std::string(), parent);
}

FormatValue::OutputKey FormatValue::AsyncAppend(NodeType type, std::string name,
                                                OutputKey parent) {
  OutputNode* parent_node = reinterpret_cast<OutputNode*>(parent);
  auto new_node = std::make_unique<OutputNode>();
  new_node->type = type;
  new_node->name = std::move(name);
  new_node->pending = true;

  pending_resolution_++;

  // The OutputKey is secretly the pointer to the node (as an intptr_t). See
  // the OutputKey definition in the header for more.
  OutputKey result = reinterpret_cast<OutputKey>(new_node.get());
  parent_node->child.push_back(std::move(new_node));
  return result;
}

void FormatValue::OutputKeyComplete(OutputKey key) {
  // See OutputKey definition in the header for how it works.
  OutputNode* dest = reinterpret_cast<OutputNode*>(key);

  // Asnyc sets should always be pending.
  FXL_DCHECK(dest->pending);
  dest->pending = false;

  // Decrement the pending count.
  FXL_DCHECK(pending_resolution_ > 0);
  pending_resolution_--;
  CheckPendingResolution();
}

void FormatValue::OutputKeyComplete(OutputKey key, OutputBuffer contents) {
  AppendToOutputKey(key, std::move(contents));
  OutputKeyComplete(key);
}

void FormatValue::CheckPendingResolution() {
  // Pending resolution could be zero before Complete() was called to set the
  // callback (the format result was synchronous) in which case ignore.
  if (pending_resolution_ != 0 || !complete_callback_)
    return;

  OutputBuffer out;
  RecursiveCollectOutput(&root_, &out);

  // The callback may be holding a ref to us, so we need to clear it
  // explicitly. But it could indirectly cause us to be deleted so need to
  // not dereference |this| after running it. This temporary will do things
  // in the order we need.
  auto cb = std::move(complete_callback_);
  cb(std::move(out));
  // WARNING: |this| may be deleted!
}

void FormatValue::RecursiveCollectOutput(OutputNode* node, OutputBuffer* out) {
  // Everything should be resolved when producing output.
  FXL_DCHECK(!node->pending);

  if (!node->name.empty()) {
    Syntax syntax;
    switch (node->type) {
      case NodeType::kGeneric:
        syntax = Syntax::kNormal;
        break;
      case NodeType::kVariable:
        syntax = Syntax::kVariable;
        break;
      case NodeType::kBaseClass:
        syntax = Syntax::kComment;
        break;
    }
    out->Append(syntax, std::move(node->name));
    out->Append(" = ");
  }

  // Each node should either have children or a buffer, but not both.
  if (node->child.empty()) {
    out->Append(std::move(node->buffer));
  } else {
    for (auto& child : node->child)
      RecursiveCollectOutput(child.get(), out);
  }
}

}  // namespace zxdb
