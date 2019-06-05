// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_

#include <map>
#include <string>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

class MockEvalContext : public EvalContext {
 public:
  MockEvalContext();
  ~MockEvalContext();

  MockSymbolDataProvider* data_provider() { return data_provider_.get(); }

  // Adds the given mocked variable with the given name and value.
  void AddVariable(const std::string& name, ExprValue v);

  // Adds a definition for the given mocked type for returning from
  // ResolveForwardDefinition() and GetConcreteType().
  void AddType(fxl::RefPtr<Type> type);

  // EvalContext implementation.
  void GetNamedValue(const ParsedIdentifier& ident,
                     ValueCallback cb) const override;
  void GetVariableValue(fxl::RefPtr<Variable> variable,
                        ValueCallback cb) const override;
  fxl::RefPtr<Type> ResolveForwardDefinition(const Type* type) const override;
  fxl::RefPtr<Type> GetConcreteType(const Type* type) const override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;
  NameLookupCallback GetSymbolNameLookupCallback() override;

 private:
  fxl::RefPtr<MockSymbolDataProvider> data_provider_;
  std::map<std::string, ExprValue> values_;
  std::map<std::string, fxl::RefPtr<Type>> types_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EVAL_CONTEXT_H_
