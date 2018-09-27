// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"

#include <ctype.h>
#include <string.h>

#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_array.h"
#include "garnet/bin/zxdb/expr/resolve_member.h"
#include "garnet/bin/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/bin/zxdb/symbols/array_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/struct_class.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

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

OutputBuffer InvalidPointerToOutput(uint64_t address) {
  OutputBuffer out;
  out.Append(OutputBuffer(fxl::StringPrintf("0x%" PRIx64 " ", address)));
  out.Append(ErrStringToOutput("invalid pointer"));
  return out;
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
bool IsCharacterType(const Type* type) {
  if (!type)
    return false;
  const Type* concrete = type->GetConcreteType();

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
bool IsCharacterType(const LazySymbol& symbol) {
  return IsCharacterType(symbol.Get()->AsType());
}

}  // namespace

FormatValue::FormatValue() : weak_factory_(this) {}
FormatValue::~FormatValue() = default;

void FormatValue::AppendValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                              const ExprValue value,
                              const FormatValueOptions& options) {
  FormatExprValue(data_provider, value, options,
                  AsyncAppend(GetRootOutputKey()));
}

void FormatValue::AppendVariable(const SymbolContext& symbol_context,
                                 fxl::RefPtr<SymbolDataProvider> data_provider,
                                 const Variable* var,
                                 const FormatValueOptions& options) {
  OutputKey output_key = AsyncAppend(GetRootOutputKey());
  auto resolver = std::make_unique<SymbolVariableResolver>(data_provider);

  // We can capture "this" here since the callback will be scoped to the
  // lifetime of the resolver which this class owns.
  resolver->ResolveVariable(
      symbol_context, var, [this, data_provider, options, output_key](
                               const Err& err, ExprValue val) {
        // The variable has been resolved, now we need to print it (which could
        // in itself be asynchronous).
        FormatExprValue(data_provider, err, val, options, output_key);
      });

  // Keep in our class scope so the callbacks will be run.
  resolvers_.push_back(std::move(resolver));
}

void FormatValue::AppendVariableWithName(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var,
    const FormatValueOptions& options) {
  Append(OutputBuffer(Syntax::kVariable, var->GetAssignedName()));
  Append(OutputBuffer(" = "));
  AppendVariable(symbol_context, std::move(data_provider), var, options);
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

void FormatValue::FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                                  const ExprValue& value,
                                  const FormatValueOptions& options,
                                  OutputKey output_key) {
  const Type* type = value.type();
  if (!type) {
    OutputKeyComplete(output_key, ErrStringToOutput("no type"));
    return;
  }
  type = type->GetConcreteType();  // Trim "const", "volatile", etc.

  // Structs and classes.
  if (const StructClass* sc = type->AsStructClass()) {
    FormatStructClass(data_provider, sc, value, options, output_key);
    return;
  }

  // Arrays and strings.
  if (TryFormatArrayOrString(data_provider, type, value, options, output_key))
    return;

  // References (these require asynchronous calls to format so can't be in the
  // "modified types" block below in the synchronous section).
  if (type->tag() == Symbol::kTagReferenceType) {
    FormatReference(data_provider, value, options, output_key);
    return;
  }

  // Everything below here is formatted synchronously. Do not early return
  // since the bottom of this function sets the output and marks the output key
  // resolved.
  OutputBuffer out;

  if (const ModifiedType* modified_type = type->AsModifiedType()) {
    // Modified types (references were handled above).
    switch (modified_type->tag()) {
      case Symbol::kTagPointerType:
        FormatPointer(value, &out);
        break;
      default:
        out.Append(Syntax::kComment,
                   fxl::StringPrintf(
                       "<Unhandled type modifier 0x%x, please file a bug.>",
                       static_cast<unsigned>(modified_type->tag())));
        break;
    }
  } else if (IsNumericBaseType(value.GetBaseType()) &&
             options.num_format != FormatValueOptions::NumFormat::kDefault) {
    // Numeric types with an overridden format option.
    switch (options.num_format) {
      case FormatValueOptions::NumFormat::kUnsigned:
      case FormatValueOptions::NumFormat::kHex:
        FormatUnsignedInt(value, options, &out);
        break;
      case FormatValueOptions::NumFormat::kSigned:
        FormatSignedInt(value, &out);
        break;
      case FormatValueOptions::NumFormat::kChar:
        FormatChar(value, &out);
        break;
      case FormatValueOptions::NumFormat::kDefault:
        // Prevent warning for unused enum type.
        break;
    }
  } else {
    // Default handling for base types based on the number.
    switch (value.GetBaseType()) {
      case BaseType::kBaseTypeAddress: {
        // Always print addresses as unsigned hex.
        FormatValueOptions overridden(options);
        overridden.num_format = FormatValueOptions::NumFormat::kHex;
        FormatUnsignedInt(value, options, &out);
        break;
      }
      case BaseType::kBaseTypeBoolean:
        FormatBoolean(value, &out);
        break;
      case BaseType::kBaseTypeFloat:
        FormatFloat(value, &out);
        break;
      case BaseType::kBaseTypeSigned:
        FormatSignedInt(value, &out);
        break;
      case BaseType::kBaseTypeUnsigned:
        FormatUnsignedInt(value, options, &out);
        break;
      case BaseType::kBaseTypeSignedChar:
      case BaseType::kBaseTypeUnsignedChar:
      case BaseType::kBaseTypeUTF:
        FormatChar(value, &out);
        break;
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

void FormatValue::FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                                  const Err& err, const ExprValue& value,
                                  const FormatValueOptions& options,
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
    FormatExprValue(std::move(data_provider), value, options, output_key);
  }
}

// GDB format:
//   {a = 1, b = 2, sub_struct = {foo = 1, bar = 2}}
//
// LLDB format:
//   {
//     a = 1
//     b = 2
//     sub_struct = {
//       foo = 1
//       bar = 2
//     }
//   }
void FormatValue::FormatStructClass(
    fxl::RefPtr<SymbolDataProvider> data_provider, const StructClass* sc,
    const ExprValue& value, const FormatValueOptions& options,
    OutputKey output_key) {
  AppendToOutputKey(output_key, OutputBuffer("{"));

  for (size_t i = 0; i < sc->data_members().size(); i++) {
    const DataMember* member = sc->data_members()[i].Get()->AsDataMember();
    if (!member)
      continue;

    if (i > 0)
      AppendToOutputKey(output_key, OutputBuffer(", "));

    AppendToOutputKey(
        output_key, OutputBuffer(Syntax::kVariable, member->GetAssignedName()));
    AppendToOutputKey(output_key, OutputBuffer(" = "));

    ExprValue member_value;
    Err err = ResolveMember(value, member, &member_value);
    FormatExprValue(data_provider, err, member_value, options,
                    AsyncAppend(output_key));
  }
  AppendToOutputKey(output_key, OutputBuffer("}"));
  OutputKeyComplete(output_key);
}

void FormatValue::FormatString(fxl::RefPtr<SymbolDataProvider> data_provider,
                               const ExprValue& value,
                               const Type* array_value_type,
                               int known_elt_count,
                               const FormatValueOptions& options,
                               OutputKey output_key) {}

bool FormatValue::TryFormatArrayOrString(
    fxl::RefPtr<SymbolDataProvider> data_provider, const Type* type,
    const ExprValue& value, const FormatValueOptions& options,
    OutputKey output_key) {
  FXL_DCHECK(type == type->GetConcreteType());

  if (type->tag() == Symbol::kTagPointerType) {
    // Any pointer type (we only char about char*).
    const ModifiedType* modified = type->AsModifiedType();
    if (!modified)
      return false;

    if (IsCharacterType(modified->modified())) {
      FormatCharPointer(data_provider, type, value, options, output_key);
      return true;
    }
    return false;  // All other pointer types are unhandled.
  } else if (type->tag() == Symbol::kTagArrayType) {
    // Any array type with a known size (we care about both).
    const ArrayType* array = type->AsArrayType();
    if (!array)
      return false;

    if (IsCharacterType(array->value_type())) {
      size_t length = array->num_elts();
      bool truncated = false;
      if (length > options.max_array_size) {
        length = options.max_array_size;
        truncated = true;
      }
      FormatCharArray(value.data().data(), length, truncated, output_key);
    } else {
      FormatArray(data_provider, value, array->num_elts(), options, output_key);
    }
    return true;
  }
  return false;
}

void FormatValue::FormatCharPointer(
    fxl::RefPtr<SymbolDataProvider> data_provider, const Type* type,
    const ExprValue& value, const FormatValueOptions& options,
    OutputKey output_key) {
  if (value.data().size() != sizeof(uint64_t)) {
    OutputKeyComplete(output_key, ErrStringToOutput("Bad pointer data."));
    return;
  }

  uint64_t address = value.GetAs<uint64_t>();
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

  data_provider->GetMemoryAsync(address, bytes_to_fetch, [
    address, bytes_to_fetch, weak_this = weak_factory_.GetWeakPtr(), output_key
  ](const Err& err, std::vector<uint8_t> data) {
    if (!weak_this)
      return;

    if (data.empty()) {
      // Should not have requested 0 size, so it if came back empty the
      // pointer was invalid.
      weak_this->OutputKeyComplete(output_key, InvalidPointerToOutput(address));
      return;
    }

    // Report as truncated because if the string goes to the end of this array
    // it will be. FormatCharArray will clear this flag if it finds a null
    // before the end of the buffer.
    //
    // Don't want to set truncated if the data ended before the requested size,
    // this means it hit the end of valid memory, so we're not omitting data
    // by only showing that part of it.
    bool truncated = data.size() == bytes_to_fetch;
    weak_this->FormatCharArray(&data[0], data.size(), truncated, output_key);
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
  for (size_t i = 0; i < output_len; i++) {
    char ch = data[i];
    if (ch == '\'' || ch == '\"' || ch == '\\') {
      // These characters get backslash-escaped.
      result.push_back('\\');
      result.push_back(ch);
    } else if (ch == '\n') {
      result += "\\n";
    } else if (ch == '\r') {
      result += "\\r";
    } else if (ch == '\t') {
      result += "\\t";
    } else if (isprint(ch)) {
      result.push_back(ch);
    } else {
      // Hex-encode everything else.
      result += fxl::StringPrintf("\\x%02x", static_cast<unsigned>(ch));
    }
  }
  result.push_back('"');

  // Add an indication if the string was truncated to the max size.
  if (truncated)
    result += "...";

  OutputKeyComplete(output_key, OutputBuffer(result));
}

void FormatValue::FormatArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                              const ExprValue& value, int elt_count,
                              const FormatValueOptions& options,
                              OutputKey output_key) {
  // Arrays should have known non-zero sizes.
  FXL_DCHECK(elt_count >= 0);
  int print_count =
      std::min(static_cast<int>(options.max_array_size), elt_count);

  std::vector<ExprValue> items;
  Err err = ResolveArray(value, 0, print_count, &items);
  if (err.has_error()) {
    OutputKeyComplete(output_key, ErrToOutput(err));
    return;
  }

  AppendToOutputKey(output_key, OutputBuffer("["));

  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0)
      AppendToOutputKey(output_key, OutputBuffer(", "));
    FormatExprValue(data_provider, items[i], options, AsyncAppend(output_key));
  }

  AppendToOutputKey(
      output_key,
      OutputBuffer(static_cast<uint32_t>(elt_count) > items.size() ? ", ...]"
                                                                   : "]"));

  // Now we can mark the root output key as complete. The children added above
  // may or may not have completed synchronously.
  OutputKeyComplete(output_key);
}

void FormatValue::FormatBoolean(const ExprValue& value, OutputBuffer* out) {
  uint64_t int_val = 0;
  Err err = value.PromoteToUint64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else if (int_val)
    out->Append("true");
  else
    out->Append("false");
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
  Err err = value.PromoteToInt64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else
    out->Append(fxl::StringPrintf("%" PRId64, int_val));
}

void FormatValue::FormatUnsignedInt(const ExprValue& value,
                                    const FormatValueOptions& options,
                                    OutputBuffer* out) {
  // This formatter handles unsigned and hex output.
  uint64_t int_val = 0;
  Err err = value.PromoteToUint64(&int_val);
  if (err.has_error())
    out->Append(ErrToOutput(err));
  else if (options.num_format == FormatValueOptions::NumFormat::kHex)
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
  char c = static_cast<char>(value.data()[0]);
  out->Append(fxl::StringPrintf("'%c'", c));
}

void FormatValue::FormatPointer(const ExprValue& value, OutputBuffer* out) {
  // Expect all pointers to be 8 bytes.
  Err err = value.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    out->Append(ErrToOutput(err));
  } else {
    uint64_t pointer_value = value.GetAs<uint64_t>();
    out->Append(
        Syntax::kComment,
        fxl::StringPrintf("(%s) ", value.type()->GetFullName().c_str()));
    out->Append(fxl::StringPrintf("0x%" PRIx64, pointer_value));
  }
}

void FormatValue::FormatReference(fxl::RefPtr<SymbolDataProvider> data_provider,
                                  const ExprValue& value,
                                  const FormatValueOptions& options,
                                  OutputKey output_key) {
  EnsureResolveReference(data_provider, value, [
    weak_this = weak_factory_.GetWeakPtr(), data_provider,
    original_value = value, options, output_key
  ](const Err& err, ExprValue resolved_value) {
    if (!weak_this)
      return;

    // First show the type.
    OutputBuffer out;
    out.Append(Syntax::kComment,
               fxl::StringPrintf("(%s) ",
                                 original_value.type()->GetFullName().c_str()));

    // Followed by the address.
    uint64_t address = 0;
    Err addr_err = original_value.PromoteToUint64(&address);
    if (addr_err.has_error()) {
      // Invalid data in the reference.
      out.Append(ErrToOutput(addr_err));
      weak_this->OutputKeyComplete(output_key, std::move(out));
      return;
    }
    out.Append(Syntax::kComment,
               fxl::StringPrintf("0x%" PRIx64 " = ", address));

    // Follow with the resolved value.
    if (err.has_error()) {
      out.Append(ErrToOutput(err));
      weak_this->OutputKeyComplete(output_key, std::move(out));
    } else {
      // FormatExprValue will mark the output key complete when it's done
      // formatting.
      weak_this->AppendToOutputKey(output_key, std::move(out));
      weak_this->FormatExprValue(data_provider, resolved_value, options,
                                 output_key);
    }
  });
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
  OutputNode* parent_node = reinterpret_cast<OutputNode*>(parent);
  auto new_node = std::make_unique<OutputNode>();
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

void FormatValue::RecursiveCollectOutput(const OutputNode* node,
                                         OutputBuffer* out) {
  // Everything should be reolved when producing output.
  FXL_DCHECK(!node->pending);

  // Each node should either have children or a buffer, but not both.
  if (node->child.empty()) {
    out->Append(std::move(node->buffer));
  } else {
    for (auto& child : node->child)
      RecursiveCollectOutput(child.get(), out);
  }
}

}  // namespace zxdb
