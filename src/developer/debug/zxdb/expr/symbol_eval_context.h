// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_SYMBOL_EVAL_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_SYMBOL_EVAL_CONTEXT_H_

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/expr/expr_eval_context.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/symbol_variable_resolver.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class CodeBlock;
class LazySymbol;
class Location;
class ProcessSymbols;
class SymbolDataProvider;
class Variable;

// An implementation of ExprEvalContext that integrates with the DWARF symbol
// system. It will provide the values of variables currently in scope.
//
// This object is reference counted since it requires asynchronous operations
// in some cases. This means it can outlive the scope in which it was invoked
// (say if the thread was resumed or the process was killed).
//
// Generally the creator of this context will be something representing that
// context in the running program like a stack frame. This frame should call
// DisownContext() when it is destroyed to ensure that evaluation does not use
// any invalid context.
class SymbolEvalContext : public ExprEvalContext {
 public:
  // The ProcessSymbols can be a null weak pointer in which case globals will
  // not be resolved (this can make testing easier).
  SymbolEvalContext(fxl::WeakPtr<const ProcessSymbols> process_symbols,
                    const SymbolContext& symbol_context,
                    fxl::RefPtr<SymbolDataProvider> data_provider,
                    fxl::RefPtr<CodeBlock> code_block);
  SymbolEvalContext(fxl::WeakPtr<const ProcessSymbols> process_symbols,
                    fxl::RefPtr<SymbolDataProvider> data_provider,
                    const Location& location);
  ~SymbolEvalContext() override;

  // ExprEvalContext implementation.
  void GetNamedValue(const ParsedIdentifier& name, ValueCallback cb) override;
  SymbolVariableResolver& GetVariableResolver() override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;
  NameLookupCallback GetSymbolNameLookupCallback() override;

 private:
  struct SearchResult {
    enum Status {
      // The variable was found, the value is set and there is no Err.
      kFound,

      // The variable was not found. The Err object will not be set (not finding
      // a variable for a given phase of lookup isn't necessarily an error).
      kNotFound,

      // There was a problem and evaluation should not continue. For example,
      // the name may be ambiguous. The Err object will be set.
      kError
    };

    SearchResult() = default;
    SearchResult(Status s, Err e = Err(),
                 fxl::RefPtr<Symbol> sym = fxl::RefPtr<Symbol>())
        : status(s), err(std::move(e)), symbol(std::move(sym)) {}

    Status status = kNotFound;
    Err err;
    fxl::RefPtr<Symbol> symbol;
  };

  // Computes the value of the given variable and issues the callback (possibly
  // asynchronously, possibly not).
  void DoResolve(FoundName found, ValueCallback cb) const;

  // Implements type name lookup on the target's symbol index.
  FoundName DoTargetSymbolsNameLookup(const ParsedIdentifier& ident);

  fxl::WeakPtr<const ProcessSymbols> process_symbols_;  // Possibly null.
  SymbolContext symbol_context_;
  fxl::RefPtr<SymbolDataProvider> data_provider_;  // Possibly null.
  SymbolVariableResolver resolver_;

  // Innermost block of the current context. May be null if there is none
  // (this means you won't get any local variable lookups).
  fxl::RefPtr<const CodeBlock> block_;

  fxl::WeakPtrFactory<SymbolEvalContext> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_SYMBOL_EVAL_CONTEXT_H_
