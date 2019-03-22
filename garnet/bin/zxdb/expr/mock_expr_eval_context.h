// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>

#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

class MockExprEvalContext : public ExprEvalContext {
 public:
  MockExprEvalContext();
  ~MockExprEvalContext();

  MockSymbolDataProvider* data_provider() { return data_provider_.get(); }

  // Adds the given mocked variable with the given name and value.
  void AddVariable(const std::string& name, ExprValue v);

  // ExprEvalContext implementation.
  void GetNamedValue(
      const Identifier& ident,
      std::function<void(const Err&, fxl::RefPtr<Symbol>, ExprValue)> cb)
      override;
  SymbolVariableResolver& GetVariableResolver() override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;
  NameLookupCallback GetSymbolNameLookupCallback() override;

 private:
  fxl::RefPtr<MockSymbolDataProvider> data_provider_;
  SymbolVariableResolver resolver_;
  std::map<std::string, ExprValue> values_;
};

}  // namespace zxdb
