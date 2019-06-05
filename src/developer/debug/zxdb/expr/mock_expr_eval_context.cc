// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/mock_expr_eval_context.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

MockExprEvalContext::MockExprEvalContext()
    : data_provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

MockExprEvalContext::~MockExprEvalContext() = default;

void MockExprEvalContext::AddVariable(const std::string& name, ExprValue v) {
  values_[name] = v;
}

void MockExprEvalContext::AddType(fxl::RefPtr<Type> type) {
  types_[type->GetFullName()] = type;
}

// ExprEvalContext implementation.
void MockExprEvalContext::GetNamedValue(const ParsedIdentifier& ident,
                                        ValueCallback cb) const {
  // Can ignore the symbol output for this test, it's not needed by the
  // expression evaluation system.
  auto found = values_.find(ident.GetFullName());
  if (found == values_.end())
    cb(Err("Not found"), nullptr, ExprValue());
  else
    cb(Err(), nullptr, found->second);
}

void MockExprEvalContext::GetVariableValue(fxl::RefPtr<Variable> variable,
                                           ValueCallback cb) const {
  cb(Err("Not found"), nullptr, ExprValue());
}

fxl::RefPtr<Type> MockExprEvalContext::ResolveForwardDefinition(
    const Type* type) const {
  auto found = types_.find(type->GetFullName());
  if (found == types_.end())  // Not found, return the input.
    return fxl::RefPtr<Type>(const_cast<Type*>(type));
  return found->second;
}

fxl::RefPtr<Type> MockExprEvalContext::GetConcreteType(const Type* type) const {
  if (!type)
    return nullptr;
  return ResolveForwardDefinition(type->StripCVT());
}

fxl::RefPtr<SymbolDataProvider> MockExprEvalContext::GetDataProvider() {
  return data_provider_;
}

NameLookupCallback MockExprEvalContext::GetSymbolNameLookupCallback() {
  // This mock version just integrates with builtin types.
  return [](const ParsedIdentifier& ident, const FindNameOptions& opts) {
    if (opts.find_types) {
      if (auto type = GetBuiltinType(ident.GetFullName()))
        return FoundName(std::move(type));
    }
    return FoundName();
  };
}

}  // namespace zxdb
