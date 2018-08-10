// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"

#include <inttypes.h>

#include <algorithm>

#include "garnet/bin/zxdb/client/symbols/symbol_context.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/type.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Generates some text describing the validity ranges for a VariableLocation
// for use in error messages where a variable is not valid.
//
// When the debugger is stable we probably want to remove this as it is very
// noisy and not useful. But with symbol and variable handling is in active
// development, listing this information can be very helpful.
std::string DescribeLocationMissError(const SymbolContext& symbol_context,
                                      uint64_t ip,
                                      const VariableLocation& loc) {
  if (loc.locations().empty())
    return "Completely optimized out.";

  // Describe ranges.
  std::string result = fxl::StringPrintf("IP = 0x%" PRIx64 ", valid", ip);
  for (const auto& entry : loc.locations()) {
    result.append(
        fxl::StringPrintf(" [0x%" PRIx64 ", 0x%" PRIx64 ")",
                          symbol_context.RelativeToAbsolute(entry.begin),
                          symbol_context.RelativeToAbsolute(entry.end)));
  }
  return result;
}

}  // namespace

SymbolVariableResolver::SymbolVariableResolver(
    fxl::RefPtr<SymbolDataProvider> data_provider)
    : data_provider_(std::move(data_provider)), weak_factory_(this) {}

SymbolVariableResolver::~SymbolVariableResolver() = default;

void SymbolVariableResolver::ResolveVariable(
    const SymbolContext& symbol_context, const Variable* var, Callback cb) {
  // Need to explicitly take a reference to the type.
  fxl::RefPtr<Type> type(const_cast<Type*>(var->type().Get()->AsType()));
  if (!type) {
    cb(Err("Missing type information for variable."), ExprValue());
    return;
  }

  const VariableLocation::Entry* loc_entry =
      var->location().EntryForIP(symbol_context, data_provider_->GetIP());
  if (!loc_entry) {
    // No DWARF location applies to the current instruction pointer.
    cb(Err(ErrType::kOptimizedOut,
           fxl::StringPrintf("The variable '%s' has been optimized out. ",
                             var->GetAssignedName().c_str()) +
               DescribeLocationMissError(
                   symbol_context, data_provider_->GetIP(), var->location())),
       ExprValue());
    return;
  }

  // Schedule the expression to be evaluated.
  dwarf_eval_.Eval(data_provider_, loc_entry->expression, [
    cb, type = std::move(type), weak_this = weak_factory_.GetWeakPtr()
  ](DwarfExprEval * eval, const Err& err) {
    if (weak_this)
      weak_this->OnDwarfEvalComplete(err, std::move(type), std::move(cb));
  });
}

void SymbolVariableResolver::OnDwarfEvalComplete(const Err& err,
                                                 fxl::RefPtr<Type> type,
                                                 Callback cb) {
  if (err.has_error()) {
    // Error decoding.
    cb(err, ExprValue());
    return;
  }

  // TODO(brettw) this needs to be much more elaborate to handle the different
  // result types. Currently this just assumes all results are integer values
  // <= uint64_t and copies the data directly.
  uint64_t result_int = dwarf_eval_.GetResult();
  size_t size_to_copy =
      std::min(static_cast<size_t>(type->byte_size()), sizeof(result_int));
  std::vector<uint8_t> data;
  data.resize(size_to_copy);
  memcpy(&data[0], &result_int, size_to_copy);

  ExprValue result(std::move(type), std::move(data));
  cb(Err(), std::move(result));
}

}  // namespace zxdb
