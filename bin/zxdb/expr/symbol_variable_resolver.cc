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
  FXL_DCHECK(!current_callback_);  // Can't have more than one pending.
  current_callback_ = std::move(cb);

  // Need to explicitly take a reference to the type.
  fxl::RefPtr<Type> type(const_cast<Type*>(var->type().Get()->AsType()));
  if (!type) {
    OnComplete(Err("Missing type information."), ExprValue());
    return;
  }

  uint64_t ip = 0;
  if (!data_provider_->GetRegister(SymbolDataProvider::kRegisterIP, &ip)) {
    OnComplete(Err("No location available."), ExprValue());
    return;
  }

  const VariableLocation::Entry* loc_entry =
      var->location().EntryForIP(symbol_context, ip);
  if (!loc_entry) {
    // No DWARF location applies to the current instruction pointer.
    OnComplete(
        Err(ErrType::kOptimizedOut,
            fxl::StringPrintf("The variable '%s' has been optimized out. ",
                              var->GetAssignedName().c_str()) +
                DescribeLocationMissError(symbol_context, ip, var->location())),
        ExprValue());
    return;
  }

  // Schedule the expression to be evaluated.
  dwarf_eval_.Eval(data_provider_, loc_entry->expression, [
    type = std::move(type), weak_this = weak_factory_.GetWeakPtr()
  ](DwarfExprEval * eval, const Err& err) {
    if (weak_this)
      weak_this->OnDwarfEvalComplete(err, std::move(type));
  });
}

void SymbolVariableResolver::ResolveFromAddress(uint64_t address,
                                                fxl::RefPtr<Type> type,
                                                Callback cb) {
  FXL_DCHECK(!current_callback_);  // Can't have more than one pending.
  current_callback_ = std::move(cb);
  DoResolveFromAddress(address, std::move(type));
}

void SymbolVariableResolver::OnDwarfEvalComplete(const Err& err,
                                                 fxl::RefPtr<Type> type) {
  if (err.has_error()) {
    // Error decoding.
    OnComplete(err, ExprValue());
    return;
  }

  uint64_t result_int = dwarf_eval_.GetResult();

  if (dwarf_eval_.GetResultType() == DwarfExprEval::ResultType::kValue) {
    // The DWARF expression produced the exact value (it's not in memory).
    uint32_t type_size = type->byte_size();
    if (type_size > sizeof(uint64_t)) {
      OnComplete(
          Err(fxl::StringPrintf("Result size insufficient for type of size %u. "
                                "Please file a bug with a repro case.",
                                type_size)),
          ExprValue());
      return;
    }
    std::vector<uint8_t> data;
    data.resize(type_size);
    memcpy(&data[0], &result_int, type_size);
    OnComplete(Err(), ExprValue(std::move(type), std::move(data)));
  } else {
    // The DWARF result is a pointer to the value.
    DoResolveFromAddress(result_int, std::move(type));
  }
}

void SymbolVariableResolver::DoResolveFromAddress(uint64_t address,
                                                  fxl::RefPtr<Type> type) {
  uint32_t type_size = type->byte_size();
  data_provider_->GetMemoryAsync(address, type_size, [
    type = std::move(type), address, weak_this = weak_factory_.GetWeakPtr()
  ](const Err& err, std::vector<uint8_t> data) {
    if (!weak_this)
      return;
    if (err.has_error()) {
      weak_this->OnComplete(err, ExprValue());
    } else if (data.size() != type->byte_size()) {
      // Short read, memory is invalid.
      weak_this->OnComplete(
          Err(fxl::StringPrintf("Can't read memory at 0x%" PRIx64 ".",
                                address)),
          ExprValue());
    } else {
      weak_this->OnComplete(Err(), ExprValue(std::move(type), std::move(data),
                                             ExprValueSource(address)));
    }
  });
}

void SymbolVariableResolver::OnComplete(const Err& err, ExprValue value) {
  FXL_DCHECK(current_callback_);

  // Executing the callback could delete |this| so clear the current_callback_
  // pointer before issuing.
  Callback cb = std::move(current_callback_);

  cb(err, std::move(value));
}

}  // namespace zxdb
