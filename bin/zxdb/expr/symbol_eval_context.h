// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/expr/expr_eval_context.h"
#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/bin/zxdb/symbols/symbol.h"
#include "garnet/bin/zxdb/symbols/symbol_context.h"
#include "lib/fxl/memory/weak_ptr.h"

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
  // The ProcessSymbols can be a null weak pointer in which case, globals will
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
  void GetNamedValue(const std::string& name, ValueCallback cb) override;
  SymbolVariableResolver& GetVariableResolver() override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;

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

  // Searches the local scopes for a match on the variable
  const Variable* GetLocalVariable(const std::string& name) const;

  // Searches for a variable with the given name on a potential |this| object
  // from the current code block. This may be asynchronous. Issues the callback
  // on completion (could be from within the call stack of this function or
  // in the future from the message loop).
  void GetImplictVariableOnThis(
      const std::string& name,
      std::function<void(SearchResult, ExprValue)> cb) const;

  // Searches global variables for the given name and issues the callback
  // with the result.
  void GetNamedValueFromGlobals(const std::string& name,
                                ValueCallback cb) const;

  // Searches the given vector of values for one with the given name. If found,
  // returns it, otherwise returns null.
  const Variable* SearchVariableVector(const std::vector<LazySymbol>& vect,
                                       const std::string& search_for) const;

  // Computes the value of the given variable and issues the callback (possibly
  // asynchronously, possibly not).
  void DoResolve(const Variable* variable, ValueCallback cb) const;

  // Searches the locations (assuming they're matching the name already) and
  // finds if there is a clear result which should be used. The result could
  // also be ambiguous in which case an error will be returned.
  //
  // This is a separate static function so the tricky prioritization logic can
  // be unit tested more easily. Logically it's part of
  // GetNamedValueFromGlobals().
  static SearchResult SearchMatchingLocations(
      const std::vector<Location>& locations, const CodeBlock* source);

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
