// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
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

void FormatPointer(const ExprValue& value, const ModifiedType* modified_type,
                   OutputBuffer* out) {
  // Expect all pointers to be 8 bytes.
  Err err = value.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    out->Append("<" + err.msg() + ">");
  } else {
    uint64_t pointer_value = value.GetAs<uint64_t>();
    out->Append(fxl::StringPrintf("(%s) 0x%" PRIx64,
                                  modified_type->GetFullName().c_str(),
                                  pointer_value));
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

}  // namespace

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

  const ModifiedType* modified_type = type->AsModifiedType();
  if (modified_type) {
    switch (modified_type->tag()) {
      case Symbol::kTagPointerType:
        FormatPointer(value, modified_type, out);
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

void ValueFormatHelper::AppendVariable(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var,
    const FormatValueOptions& options) {
  // Save an empty buffer for the output to be asynchronously written to.
  size_t index = buffers_.size();
  buffers_.emplace_back();

  auto resolver =
      std::make_unique<SymbolVariableResolver>(std::move(data_provider));
  pending_resolution_++;

  // We can capture "this" here since the callback will be scoped to the
  // lifetime of the resolver which this class owns.
  resolver->ResolveVariable(
      symbol_context, var,
      [this, index, options](const Err& err, ExprValue val) {
        FormatExprValue(err, val, options, &buffers_[index]);

        FXL_DCHECK(pending_resolution_ > 0);
        pending_resolution_--;
        CheckPendingResolution();
        // WARNING: |this| may be deleted.
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

    complete_callback_(std::move(result));
    // WARNING: |this| may be deleted!
  }
}

}  // namespace zxdb
