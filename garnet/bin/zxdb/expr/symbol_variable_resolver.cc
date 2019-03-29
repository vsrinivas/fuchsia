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
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

SymbolVariableResolver::SymbolVariableResolver(
    fxl::RefPtr<SymbolDataProvider> data_provider)
    : data_provider_(std::move(data_provider)), weak_factory_(this) {}

SymbolVariableResolver::~SymbolVariableResolver() = default;

void SymbolVariableResolver::ResolveVariable(
    const SymbolContext& symbol_context, const Variable* var,
    Callback cb) const {
  auto state = fxl::MakeRefCounted<ResolutionState>(std::move(cb));

  // Need to explicitly take a reference to the type.
  fxl::RefPtr<Type> type(const_cast<Type*>(var->type().Get()->AsType()));
  if (!type) {
    OnComplete(state, Err("Missing type information."), ExprValue());
    return;
  }

  auto ip = data_provider_->GetRegister(debug_ipc::GetSpecialRegisterID(
      data_provider_->GetArch(), debug_ipc::SpecialRegisterType::kIP));
  if (!ip) {
    OnComplete(state, Err("No location available."), ExprValue());
    return;
  }

  const VariableLocation::Entry* loc_entry =
      var->location().EntryForIP(symbol_context, *ip);
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
    OnComplete(state, Err(ErrType::kOptimizedOut, std::move(err_str)),
               ExprValue());
    return;
  }

  // Schedule the expression to be evaluated.
  state->dwarf_eval.Eval(
      data_provider_, symbol_context, loc_entry->expression,
      [state, type = std::move(type), weak_this = weak_factory_.GetWeakPtr()](
          DwarfExprEval* eval, const Err& err) {
        if (weak_this)
          weak_this->OnDwarfEvalComplete(state, err, std::move(type));
      });
}

void SymbolVariableResolver::OnDwarfEvalComplete(
    fxl::RefPtr<ResolutionState> state, const Err& err,
    fxl::RefPtr<Type> type) const {
  if (err.has_error()) {
    // Error decoding.
    OnComplete(state, err, ExprValue());
    return;
  }

  uint64_t result_int = state->dwarf_eval.GetResult();

  // The DWARF expression will produce either the address of the value or the
  // value itself.
  if (state->dwarf_eval.GetResultType() == DwarfExprEval::ResultType::kValue) {
    // The DWARF expression produced the exact value (it's not in memory).
    uint32_t type_size = type->byte_size();
    if (type_size > sizeof(uint64_t)) {
      OnComplete(
          state,
          Err(fxl::StringPrintf("Result size insufficient for type of size %u. "
                                "Please file a bug with a repro case.",
                                type_size)),
          ExprValue());
      return;
    }
    std::vector<uint8_t> data;
    data.resize(type_size);
    memcpy(&data[0], &result_int, type_size);
    OnComplete(state, Err(), ExprValue(std::move(type), std::move(data)));
  } else {
    // The DWARF result is a pointer to the value.
    ResolvePointer(data_provider_, result_int, std::move(type),
                   [state, weak_this = weak_factory_.GetWeakPtr()](
                       const Err& err, ExprValue value) {
                     if (weak_this)
                       weak_this->OnComplete(state, err, std::move(value));
                   });
  }
}

void SymbolVariableResolver::OnComplete(fxl::RefPtr<ResolutionState> state,
                                        const Err& err, ExprValue value) const {
  // WARNING: executing the callback can delete |this|.
  state->callback(err, std::move(value));
}

}  // namespace zxdb
