// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_value.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

void FormatValue(const Value* value, OutputBuffer* out) {
  out->Append(Syntax::kVariable, value->GetAssignedName());
  out->Append(" = TODO");
}

void FormatExprValue(const ExprValue& value, OutputBuffer* out) {
  switch (value.GetBaseType()) {
    case BaseType::kBaseTypeSigned: {
      int64_t int_val = 0;
      switch (value.data().size()) {
        case 1:
          int_val = value.GetAs<int8_t>();
          break;
        case 2:
          int_val = value.GetAs<int16_t>();
          break;
        case 4:
          int_val = value.GetAs<int32_t>();
          break;
        case 8:
          int_val = value.GetAs<int64_t>();
          break;
        default:
          out->Append("<unknown signed integer type>");
          return;
      }
      out->Append(fxl::StringPrintf("%" PRId64, int_val));
      break;
    }
    case BaseType::kBaseTypeUnsigned: {
      uint64_t int_val = 0;
      switch (value.data().size()) {
        case 1:
          int_val = value.GetAs<uint8_t>();
          break;
        case 2:
          int_val = value.GetAs<uint16_t>();
          break;
        case 4:
          int_val = value.GetAs<uint32_t>();
          break;
        case 8:
          int_val = value.GetAs<uint64_t>();
          break;
        default:
          out->Append("<unknown unsigned integer type>");
          return;
      }
      out->Append(fxl::StringPrintf("%" PRIu64, int_val));
      break;
    }
    default: {
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
                     OutputBuffer* out) {
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
    FormatExprValue(value, out);
  }
}

ValueFormatHelper::ValueFormatHelper() = default;
ValueFormatHelper::~ValueFormatHelper() = default;

void ValueFormatHelper::AppendVariable(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var) {
  // Save an empty buffer for the output to be asynchronously written to.
  size_t index = buffers_.size();
  buffers_.emplace_back();

  auto resolver =
      std::make_unique<SymbolVariableResolver>(std::move(data_provider));
  pending_resolution_++;

  // We can capture "this" here since the callback will be scoped to the
  // lifetime of the resolver which this class owns.
  resolver->ResolveVariable(symbol_context, var,
                            [this, index](const Err& err, ExprValue val) {
                              FormatExprValue(err, val, &buffers_[index]);

                              FXL_DCHECK(pending_resolution_ > 0);
                              pending_resolution_--;
                              CheckPendingResolution();
                            });

  // Keep in our class scope so the callbacks will be run.
  resolvers_.push_back(std::move(resolver));
}

void ValueFormatHelper::AppendVariableWithName(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Variable* var) {
  Append(OutputBuffer::WithContents(Syntax::kVariable, var->GetAssignedName()));
  Append(OutputBuffer::WithContents(" = "));
  AppendVariable(symbol_context, std::move(data_provider), var);
}

void ValueFormatHelper::Append(OutputBuffer out) {
  buffers_.push_back(std::move(out));
}

void ValueFormatHelper::Complete(Callback callback) {
  FXL_DCHECK(!complete_callback_);
  complete_callback_ = std::move(callback);

  // If there are no pending formats, issue the callback right away.
  CheckPendingResolution();
}

void ValueFormatHelper::CheckPendingResolution() {
  // Pending resolution could be zero before Complete() was called to set the
  // callback (the format result was synchronous) in which case ignore.
  if (pending_resolution_ == 0 && complete_callback_) {
    OutputBuffer result;
    for (auto& cur : buffers_)
      result.Append(std::move(cur));

    complete_callback_(std::move(result));
    complete_callback_ = Callback();
  }
}

}  // namespace zxdb
