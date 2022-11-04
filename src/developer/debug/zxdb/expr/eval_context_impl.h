// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_IMPL_H_

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/pretty_type_manager.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class CodeBlock;
class DwarfExprEval;
class LazySymbol;
class Location;
class ProcessSymbols;
class SymbolDataProvider;
class Variable;

// An implementation of EvalContext that integrates with the DWARF symbol system. It will provide
// the values of variables currently in scope.
//
// This object is reference counted since it requires asynchronous operations in some cases. This
// means it can outlive the scope in which it was invoked (say if the thread was resumed or the
// process was killed).
//
// Generally the creator of this context will be something representing that context in the running
// program like a stack frame. This frame should call DisownContext() when it is destroyed to ensure
// that evaluation does not use any invalid context.
class EvalContextImpl : public EvalContext {
 public:
  // Construct with fxl::MakeRefCounted.

  // EvalContext implementation.
  //
  // NOTE: Some of these implementations return constant values because the expression library
  // doesn't have enough context to know what they should be. The ClientEvalContextImpl hooks
  // some things up to the debugger settings system.
  ExprLanguage GetLanguage() const override;
  const std::shared_ptr<Abi>& GetAbi() const override { return abi_; }
  using EvalContext::FindName;
  void FindName(const FindNameOptions& options, const ParsedIdentifier& looking_for,
                std::vector<FoundName>* results) const override;
  FindNameContext GetFindNameContext() const override;
  void GetNamedValue(const ParsedIdentifier& name, EvalCallback cb) const override;
  void GetVariableValue(fxl::RefPtr<Value> variable, EvalCallback cb) const override;
  const ProcessSymbols* GetProcessSymbols() const override;
  fxl::RefPtr<SymbolDataProvider> GetDataProvider() override;
  Location GetLocationForAddress(uint64_t address) const override;
  const PrettyTypeManager& GetPrettyTypeManager() const override { return pretty_type_manager_; }
  VectorRegisterFormat GetVectorRegisterFormat() const override {
    return VectorRegisterFormat::kDouble;
  }
  bool ShouldPromoteToDerived() const override { return false; }

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(EvalContextImpl);
  FRIEND_MAKE_REF_COUNTED(EvalContextImpl);

  // All of the input pointers can be null:
  //
  //  - The ProcessSymbols can be a null weak pointer in which case globals will not be resolved.
  //    This can make testing easier and supports evaluating math without a loaded program.
  //
  //  - The SymbolDataProvider can be null in which case anything that requires memory from the
  //    target will fail. Some operations like pure math don't require this.
  //
  //  - The code block can be null in which case nothing using the current scope will work. This
  //    includes local variables, variables on "this", and things relative to the current namespace.
  //
  // The variant that takes a location will extract the code block from the location if possible.
  EvalContextImpl(std::shared_ptr<Abi> abi, fxl::WeakPtr<const ProcessSymbols> process_symbols,
                  fxl::RefPtr<SymbolDataProvider> data_provider, ExprLanguage language,
                  fxl::RefPtr<CodeBlock> code_block = fxl::RefPtr<CodeBlock>());
  EvalContextImpl(std::shared_ptr<Abi> abi, fxl::WeakPtr<const ProcessSymbols> process_symbols,
                  fxl::RefPtr<SymbolDataProvider> data_provider, const Location& location,
                  std::optional<ExprLanguage> force_language = std::nullopt);
  ~EvalContextImpl() override;

  void set_language(ExprLanguage lang) { language_ = lang; }

 private:
  // Converts an extern value to a real Variable by looking the name up in the index.
  Err ResolveExternValue(const fxl::RefPtr<Value>& input_value,
                         fxl::RefPtr<Variable>* resolved) const;

  // Computes the value of the given variable and issues the callback (possibly asynchronously,
  // possibly not).
  void DoResolve(FoundName found, EvalCallback cb) const;

  // Implements type name lookup on the target's symbol index.
  FoundName DoTargetSymbolsNameLookup(const ParsedIdentifier& ident);

  std::shared_ptr<Abi> abi_;

  fxl::WeakPtr<const ProcessSymbols> process_symbols_;  // Possibly null.
  fxl::RefPtr<SymbolDataProvider> data_provider_;       // Possibly null.

  // Innermost block of the current context. May be null if there is none (this means you won't get
  // any local variable lookups).
  fxl::RefPtr<const CodeBlock> block_;

  // Language extracted from the code block.
  ExprLanguage language_ = ExprLanguage::kC;

  PrettyTypeManager pretty_type_manager_;

  mutable fxl::WeakPtrFactory<EvalContextImpl> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EVAL_CONTEXT_IMPL_H_
