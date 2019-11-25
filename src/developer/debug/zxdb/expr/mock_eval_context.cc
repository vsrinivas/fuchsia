// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/mock_eval_context.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

MockEvalContext::MockEvalContext()
    : data_provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

MockEvalContext::~MockEvalContext() = default;

void MockEvalContext::AddVariable(const std::string& name, ExprValue v) {
  values_by_name_[name] = v;
}
void MockEvalContext::AddVariable(const Value* key, ExprValue v) { values_by_symbol_[key] = v; }

void MockEvalContext::AddType(fxl::RefPtr<Type> type) { types_[type->GetFullName()] = type; }

void MockEvalContext::AddLocation(uint64_t address, Location location) {
  locations_[address] = std::move(location);
}

// EvalContext implementation.
void MockEvalContext::GetNamedValue(const ParsedIdentifier& ident, ValueCallback cb) const {
  // Can ignore the symbol output for this test, it's not needed by the expression evaluation
  // system.
  auto found = values_by_name_.find(ident.GetFullName());
  if (found == values_by_name_.end()) {
    cb(Err("MockEvalContext::GetVariableValue '%s' not found.", ident.GetFullName().c_str()));
  } else {
    cb(found->second);
  }
}

void MockEvalContext::GetVariableValue(fxl::RefPtr<Value> variable, ValueCallback cb) const {
  auto found = values_by_symbol_.find(variable.get());
  if (found == values_by_symbol_.end())
    cb(Err("MockEvalContext::GetVariableValue '%s' not found.", variable->GetFullName().c_str()));
  else
    cb(found->second);
}

fxl::RefPtr<Type> MockEvalContext::ResolveForwardDefinition(const Type* type) const {
  auto found = types_.find(type->GetFullName());
  if (found == types_.end())  // Not found, return the input.
    return RefPtrTo(type);
  return found->second;
}

fxl::RefPtr<Type> MockEvalContext::GetConcreteType(const Type* type) const {
  if (!type)
    return nullptr;
  return ResolveForwardDefinition(type->StripCVT());
}

fxl::RefPtr<SymbolDataProvider> MockEvalContext::GetDataProvider() { return data_provider_; }

NameLookupCallback MockEvalContext::GetSymbolNameLookupCallback() {
  // This mock version just integrates with builtin types.
  return [lang = language_](const ParsedIdentifier& ident, const FindNameOptions& opts) {
    if (opts.find_types) {
      if (auto type = GetBuiltinType(lang, ident.GetFullName()))
        return FoundName(std::move(type));
    }
    return FoundName();
  };
}

Location MockEvalContext::GetLocationForAddress(uint64_t address) const {
  auto found = locations_.find(address);
  if (found == locations_.end())
    return Location(Location::State::kAddress, address);
  return found->second;
}

}  // namespace zxdb
