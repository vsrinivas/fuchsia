// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"

#include <inttypes.h>

#include <algorithm>

#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/symbols/symbol_context.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/type.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

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
    std::string err_str;
    if (var->location().is_null()) {
      // With no locations, this variable has been completely optimized out.
      err_str = fxl::StringPrintf("'%s' has been optimized out.",
                                  var->GetAssignedName().c_str());
    } else {
      // There are locations but none of them match the current IP.
      err_str = fxl::StringPrintf("'%s' is not available at this address. ",
                                  var->GetAssignedName().c_str());
    }
    OnComplete(Err(ErrType::kOptimizedOut, std::move(err_str)), ExprValue());
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

void SymbolVariableResolver::OnDwarfEvalComplete(const Err& err,
                                                 fxl::RefPtr<Type> type) {
  if (err.has_error()) {
    // Error decoding.
    OnComplete(err, ExprValue());
    return;
  }

  uint64_t result_int = dwarf_eval_.GetResult();

  // The DWARF expression will produce either the address of the value or the
  // value itself.
  DwarfExprEval::ResultType result_type = dwarf_eval_.GetResultType();
  if (type->GetConcreteType()->AsArrayType()) {
    // Special-case array types. When DWARF tells us the address of an array,
    // it's telling us the address of the first element. But our "array"
    // ExprValue are themselves pointers. In this case, just declare DWARF
    // produced a value result (the value of the pointer we put in the result).
    if (result_type == DwarfExprEval::ResultType::kPointer) {
      result_type = DwarfExprEval::ResultType::kValue;
    } else {
      // Don't expect the expression to produce a literal array out of thin
      // air. All of our arrays must be in memory.
      OnComplete(Err("DWARF expression produced array literal. Please file a "
                     "bug with a repro."),
                 ExprValue());
    }
  }

  if (result_type == DwarfExprEval::ResultType::kValue) {
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
    ResolvePointer(data_provider_, result_int, std::move(type),
                   [weak_this = weak_factory_.GetWeakPtr()](const Err& err,
                                                            ExprValue value) {
                     if (weak_this)
                       weak_this->OnComplete(err, std::move(value));
                   });
  }
}

void SymbolVariableResolver::OnComplete(const Err& err, ExprValue value) {
  FXL_DCHECK(current_callback_);

  // Executing the callback could delete |this| so clear the current_callback_
  // pointer before issuing.
  Callback cb = std::move(current_callback_);

  cb(err, std::move(value));
}

}  // namespace zxdb
