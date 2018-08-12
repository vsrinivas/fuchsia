// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/client/symbols/symbol_context.h"
#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class CodeBlock;
class LazySymbol;
class Location;
class SymbolDataProvider;
class Variable;

// An implementation of ExprEvalContext that integrates with the DWARF symbol
// system. It will provide the values of variables currently in scope.
class SymbolEvalContext : public ExprEvalContext {
 public:
  using Callback = std::function<void(const Err&, ExprValue)>;

  explicit SymbolEvalContext(const SymbolEvalContext&) = default;
  SymbolEvalContext(const SymbolContext& symbol_context,
                    fxl::RefPtr<SymbolDataProvider> data_provider,
                    fxl::RefPtr<CodeBlock> code_block);
  SymbolEvalContext(fxl::RefPtr<SymbolDataProvider> data_provider,
                    const Location& location);
  ~SymbolEvalContext() override = default;

  // ExprEvalContext implementation.
  void GetVariable(const std::string& name, Callback cb) override;

 private:
  // Searches the given vector of values for one with the given name. If found,
  // executes the given callback (possibly asynchronously in the future) with
  // it and returns true. False means the variable was not found.
  bool SearchVariableVector(
      const std::vector<LazySymbol>& vect, const std::string& search_for,
      Callback& cb);

  SymbolContext symbol_context_;
  SymbolVariableResolver resolver_;

  // Innermost block of the current context. May be null if there is none
  // (this means you won't get any local variable lookups).
  fxl::RefPtr<const CodeBlock> block_;

  fxl::WeakPtrFactory<SymbolEvalContext> weak_factory_;
};

}  // namespace zxdb
