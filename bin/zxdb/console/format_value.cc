// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"

#include <ctype.h>
#include <string.h>

#include "garnet/bin/zxdb/client/symbols/array_type.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_array.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void FormatBoolean(const ExprValue& value, OutputBuffer* out) {
  uint64_t int_val = 0;
  Err err = value.PromoteToUint64(&int_val);
  if (err.has_error())
    out->Append("<" + err.msg() + ">");
  else if (int_val)
    out->Append("true");
  else
    out->Append("false");
}

void FormatSignedInt(const ExprValue& value, OutputBuffer* out) {
  int64_t int_val = 0;
  Err err = value.PromoteToInt64(&int_val);
  if (err.has_error())
    out->Append("<" + err.msg() + ">");
  else
    out->Append(fxl::StringPrintf("%" PRId64, int_val));
}

// This formatted handles unsigned and hex output.
void FormatUnsignedInt(const ExprValue& value,
                       const FormatValueOptions& options, OutputBuffer* out) {
  uint64_t int_val = 0;
  Err err = value.PromoteToUint64(&int_val);
  if (err.has_error())
    out->Append("<" + err.msg() + ">");
  else if (options.num_format == FormatValueOptions::NumFormat::kHex)
    out->Append(fxl::StringPrintf("0x%" PRIx64, int_val));
  else
    out->Append(fxl::StringPrintf("%" PRIu64, int_val));
}

void FormatFloat(const ExprValue& value, OutputBuffer* out) {
  switch (value.data().size()) {
    case sizeof(float):
      out->Append(fxl::StringPrintf("%g", value.GetAs<float>()));
      break;
    case sizeof(double):
      out->Append(fxl::StringPrintf("%g", value.GetAs<double>()));
      break;
    default:
      out->Append(fxl::StringPrintf("<unknown float of size %d>",
                                    static_cast<int>(value.data().size())));
      break;
  }
}

void FormatChar(const ExprValue& value, OutputBuffer* out) {
  // Just take the first byte for all char.
  if (value.data().empty()) {
    out->Append("<invalid char type>");
    return;
  }
  char c = static_cast<char>(value.data()[0]);
  out->Append(fxl::StringPrintf("'%c'", c));
}

void FormatPointer(const ExprValue& value, OutputBuffer* out) {
  // Expect all pointers to be 8 bytes.
  Err err = value.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    out->Append("<" + err.msg() + ">");
  } else {
    uint64_t pointer_value = value.GetAs<uint64_t>();
    out->Append(fxl::StringPrintf(
        "(%s) 0x%" PRIx64, value.type()->GetFullName().c_str(), pointer_value));
  }
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

// Formats the preview of a string from known data. known_elt_count is the
// statically known element count, or -1 if not statically known. The
// bytes_requested is sent also so we can tell if the data was truncated.
OutputBuffer FormatStringData(uint64_t address, uint32_t bytes_requested,
                              int known_elt_count,
                              const std::vector<uint8_t>& data) {
  if (data.empty()) {
    // No data came back. Since FormatString filters out known-0-length strings
    // in advance, we know this case is due to a memory error.
    return OutputBuffer::WithContents(
        fxl::StringPrintf("0x%" PRIx64 " <invalid pointer>", address));
  }

  size_t output_len =
      strnlen(reinterpret_cast<const char*>(&data[0]), data.size());

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
  if (output_len == bytes_requested &&
      known_elt_count != static_cast<int>(bytes_requested))
    result += "...";

  return OutputBuffer::WithContents(result);
}

// Asynchronously formats a string type.
//
// The known_elt_count can be -1 if the array size is not statically known.
void FormatString(fxl::RefPtr<SymbolDataProvider> data_provider,
                  const ExprValue& value, const Type* array_value_type,
                  int known_elt_count, const FormatValueOptions& options,
                  std::function<void(OutputBuffer)> cb) {
  if (value.data().size() != sizeof(uint64_t)) {
    cb(OutputBuffer::WithContents("<bad pointer type>"));
    return;
  }
  uint64_t address = value.GetAs<uint64_t>();

  // Special-case null pointers to just print a null address.
  if (!address) {
    cb(OutputBuffer::WithContents("0x0"));
    return;
  }

  // Compute the bytes to fetch.
  uint32_t bytes_to_fetch;
  if (known_elt_count == -1) {
    // Speculatively request the max if the length is unknown.
    bytes_to_fetch = options.max_array_size;
  } else if (known_elt_count == 0 || options.max_array_size == 0) {
    // Known 0-length string. This shouldn't happen in C since 0-length arrays
    // are invalid, but handle anyway (maybe the user specifically requested
    // this by mistake) to prevent weirdness handling a 0 byte request later.
    cb(OutputBuffer::WithContents("\"\""));
    return;
  } else {
    // Statically known size, but still cap.
    bytes_to_fetch = std::min(options.max_array_size,
                              static_cast<uint32_t>(known_elt_count));
  }

  // Request the data and format in the callback.
  ResolveByteArray(data_provider, address, 0, bytes_to_fetch, [
    address, bytes_to_fetch, known_elt_count, cb = std::move(cb)
  ](const Err& err, std::vector<uint8_t> data) {
    if (err.has_error()) {
      cb(OutputBuffer::WithContents("<" + err.msg() + ">"));
    } else {
      cb(FormatStringData(address, bytes_to_fetch, known_elt_count, data));
    }
  });
}

void FormatArrayData(fxl::RefPtr<SymbolDataProvider> data_provider,
                     uint64_t address, const std::vector<ExprValue>& items,
                     int known_size, const FormatValueOptions& options,
                     std::function<void(OutputBuffer)> cb) {
  if (items.empty()) {
    // No data came back. Since FormatArray filters out known-0-length arrays
    // in advance, we know this case is due to a memory error.
    cb(OutputBuffer::WithContents(
        fxl::StringPrintf("0x%" PRIx64 " <invalid pointer>", address)));
    return;
  }

  auto helper = fxl::MakeRefCounted<ValueFormatHelper>();
  helper->Append("[");

  for (size_t i = 0; i < items.size(); i++) {
    if (i > 0)
      helper->Append(", ");
    helper->AppendValue(data_provider, items[i], options);
  }

  if (static_cast<uint32_t>(known_size) > items.size())
    helper->Append(", ...]");
  else
    helper->Append("]");
  helper->Complete(
      [ helper, cb = std::move(cb) ](OutputBuffer out) { cb(std::move(out)); });
}

// Asynchronously formats a non-string array.
void FormatArray(fxl::RefPtr<SymbolDataProvider> data_provider,
                 const ExprValue& value, const Type* array_value_type,
                 int elt_count, const FormatValueOptions& options,
                 std::function<void(OutputBuffer)> cb) {
  // Arrays should have known non-zero sizes.
  FXL_DCHECK(elt_count >= 0);

  if (value.data().size() != sizeof(uint64_t)) {
    cb(OutputBuffer::WithContents("<bad pointer type>"));
    return;
  }
  if (!array_value_type) {
    cb(OutputBuffer::WithContents("<bad type information>"));
    return;
  }
  uint64_t address = value.GetAs<uint64_t>();

  // Special-case null pointers to just print a null address.
  if (!address) {
    cb(OutputBuffer::WithContents("0x0"));
    return;
  }

  int fetch_count =
      std::min(static_cast<int>(options.max_array_size), elt_count);
  if (fetch_count == 0) {
    cb(OutputBuffer::WithContents("[]"));
    return;
  }

  ResolveValueArray(
      std::move(data_provider), array_value_type, address, 0, fetch_count,
      [ data_provider, address, elt_count, options, cb = std::move(cb) ](
          const Err& err, std::vector<ExprValue> items) {
        if (err.has_error()) {
          cb(OutputBuffer::WithContents("<" + err.msg() + ">"));
        } else {
          FormatArrayData(data_provider, address, items, elt_count, options,
                          std::move(cb));
        }
      });
}

// Returns true if the type should be formatted as a string. The input type
// should already be concrete.
//
// On success computes two out params: the array value type and the statically
// known array size (-1 if not statically known).
bool ShouldFormatAsString(const Type* type, const Type** array_value_type,
                          int* known_array_size) {
  FXL_DCHECK(type == type->GetConcreteType());

  const Type* value_type = nullptr;

  // Check for pointer or array types.
  if (type->tag() == Symbol::kTagPointerType) {
    const ModifiedType* modified = type->AsModifiedType();
    if (!modified)
      return false;
    value_type = modified->modified().Get()->AsType();
  } else if (type->tag() == Symbol::kTagArrayType) {
    const ArrayType* array = type->AsArrayType();
    if (!array)
      return false;
    value_type = array->value_type().Get()->AsType();
    *known_array_size = array->num_elts();
  } else {
    // Only pointers and arrays get string handling.
    return false;
  }

  // Anything above may have failed to produce a type.
  if (!value_type)
    return false;
  value_type = value_type->GetConcreteType();

  // The underlying type should be a 1-byte character type.
  // TODO(brettw) handle Unicode.
  if (value_type->byte_size() != 1)
    return false;
  const BaseType* base_type = value_type->AsBaseType();
  if (!base_type)
    return false;

  if (base_type->base_type() == BaseType::kBaseTypeSignedChar ||
      base_type->base_type() == BaseType::kBaseTypeUnsignedChar) {
    *array_value_type = value_type;
    return true;
  }
  return false;
}

}  // namespace

void FormatExprValue(fxl::RefPtr<SymbolDataProvider> data_provider,
                     const ExprValue& value, const FormatValueOptions& options,
                     std::function<void(OutputBuffer)> cb) {
  const Type* type = value.type();
  if (!type) {
    cb(OutputBuffer::WithContents("<no type>"));
    return;
  }
  type = type->GetConcreteType();

  // TODO(brettw) this should dereference reference types.

  const Type* str_array_value_type = nullptr;
  int known_elt_count = -1;
  if (ShouldFormatAsString(type, &str_array_value_type, &known_elt_count)) {
    // String formatting.
    FormatString(std::move(data_provider), value, str_array_value_type,
                 known_elt_count, options, std::move(cb));
  } else if (const ArrayType* array = type->AsArrayType()) {
    // Array formatting.
    FormatArray(std::move(data_provider), value,
                array->value_type().Get()->AsType(), known_elt_count, options,
                std::move(cb));
  } else {
    // All other types that don't need memory fetching.
    OutputBuffer output;
    FormatExprValue(value, options, &output);
    cb(std::move(output));
  }
}

void FormatValue(const Value* value, OutputBuffer* out) {
  out->Append(Syntax::kVariable, value->GetAssignedName());
  out->Append(" = TODO");
}

void FormatExprValue(const ExprValue& value, const FormatValueOptions& options,
                     OutputBuffer* out) {
  const Type* type = value.type()->GetConcreteType();
  if (!type) {
    out->Append("<no type>");
    return;
  }

  if (const ModifiedType* modified_type = type->AsModifiedType()) {
    switch (modified_type->tag()) {
      case Symbol::kTagPointerType:
        FormatPointer(value, out);
        break;
        // TODO(brettw) need to handle various reference types here.
    }
    return;
  }

  // Check for numeric types with an overridden format option.
  int base_type = value.GetBaseType();
  if (IsNumericBaseType(base_type) &&
      options.num_format != FormatValueOptions::NumFormat::kDefault) {
    switch (options.num_format) {
      case FormatValueOptions::NumFormat::kUnsigned:
      case FormatValueOptions::NumFormat::kHex:
        FormatUnsignedInt(value, options, out);
        break;
      case FormatValueOptions::NumFormat::kSigned:
        FormatSignedInt(value, out);
        break;
      case FormatValueOptions::NumFormat::kChar:
        FormatChar(value, out);
        break;
      case FormatValueOptions::NumFormat::kDefault:
        // Prevent warning for unused enum type.
        break;
    }
    return;
  }

  // Default handling for base types based on the number.
  switch (value.GetBaseType()) {
    case BaseType::kBaseTypeAddress: {
      // Always print addresses as unsigned hex.
      FormatValueOptions overridden(options);
      overridden.num_format = FormatValueOptions::NumFormat::kHex;
      FormatUnsignedInt(value, options, out);
      break;
    }
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
    default:
      if (value.data().empty()) {
        out->Append("<no data>");
      } else {
        // For now, print a hex dump for everything else.
        std::string result;
        for (size_t i = 0; i < value.data().size(); i++) {
          if (i > 0)
            result.push_back(' ');
          result.append(fxl::StringPrintf("0x%02x", value.data()[i]));
        }
        out->Append(std::move(result));
      }
  }
}

void FormatExprValue(const Err& err, const ExprValue& value,
                     const FormatValueOptions& options, OutputBuffer* out) {
  if (err.has_error()) {
    // If the future we probably want to rewrite "optimized out" errors to
    // something shorter. The evaluator makes a longer message suitable for
    // printing to the console in response to a command, but is too long
    // for printing as as the value in "foo = bar". For now, though, the longer
    // messages can be helpful for debugging. It would be:
    //   if (err.type() == ErrType::kOptimizedOut)
    //      out->Append("<optimized out>");
    out->Append("<" + err.msg() + ">");
  } else {
    FormatExprValue(value, options, out);
  }
}

ValueFormatHelper::ValueFormatHelper() = default;
ValueFormatHelper::~ValueFormatHelper() = default;

void ValueFormatHelper::AppendValue(
    fxl::RefPtr<SymbolDataProvider> data_provider, const ExprValue value,
    const FormatValueOptions& options) {
  size_t index = AddPendingResolution();

  // Callback needs to take a ref to keep class alive through callback.
  FormatExprValue(data_provider, value, options,
                  [ this_ref = fxl::RefPtr<ValueFormatHelper>(this),
                    index ](OutputBuffer out) {
                    this_ref->buffers_[index] = std::move(out);
                    this_ref->DecrementPendingResolution();
                  });
}

void ValueFormatHelper::AppendVariable(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var,
    const FormatValueOptions& options) {
  size_t index = AddPendingResolution();
  auto resolver = std::make_unique<SymbolVariableResolver>(data_provider);

  // We can capture "this" here since the callback will be scoped to the
  // lifetime of the resolver which this class owns.
  resolver->ResolveVariable(
      symbol_context, var,
      [this, data_provider, index, options](const Err& err, ExprValue val) {
        // The variable has been resolved, now we need to print it (which could
        // in itself be asynchronous). This call can't capture |this| because
        // the resolver no longer scopes the callback, so need to take a ref.
        FormatExprValue(data_provider, val, options, [
          this_ref = fxl::RefPtr<ValueFormatHelper>(this), index
        ](OutputBuffer out) {
          this_ref->buffers_[index] = std::move(out);
          this_ref->DecrementPendingResolution();
        });
      });

  // Keep in our class scope so the callbacks will be run.
  resolvers_.push_back(std::move(resolver));
}

void ValueFormatHelper::AppendVariableWithName(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var,
    const FormatValueOptions& options) {
  Append(OutputBuffer::WithContents(Syntax::kVariable, var->GetAssignedName()));
  Append(OutputBuffer::WithContents(" = "));
  AppendVariable(symbol_context, std::move(data_provider), var, options);
}

void ValueFormatHelper::Append(OutputBuffer out) {
  buffers_.push_back(std::move(out));
}

void ValueFormatHelper::Append(std::string str) {
  Append(OutputBuffer::WithContents(std::move(str)));
}

void ValueFormatHelper::Complete(Callback callback) {
  FXL_DCHECK(!complete_callback_);
  complete_callback_ = std::move(callback);

  // If there are no pending formats, issue the callback right away.
  CheckPendingResolution();
  // WARNING: |this| may be deleted.
}

void ValueFormatHelper::CheckPendingResolution() {
  // Pending resolution could be zero before Complete() was called to set the
  // callback (the format result was synchronous) in which case ignore.
  if (pending_resolution_ == 0 && complete_callback_) {
    OutputBuffer result;
    for (auto& cur : buffers_)
      result.Append(std::move(cur));

    // The callback may be holding a ref to us, so we need to clear it
    // explicitly. But it could indirectly cause us to be deleted so need to
    // not dereference |this| after running it. This temporary will do things
    // in the order we need.
    auto cb = std::move(complete_callback_);
    cb(std::move(result));
    // WARNING: |this| may be deleted!
  }
}

size_t ValueFormatHelper::AddPendingResolution() {
  size_t index = buffers_.size();
  buffers_.emplace_back();
  pending_resolution_++;
  return index;
}

void ValueFormatHelper::DecrementPendingResolution() {
  FXL_DCHECK(pending_resolution_ > 0);
  pending_resolution_--;
  CheckPendingResolution();
  // WARNING: |this| may be deleted!
}

}  // namespace zxdb
