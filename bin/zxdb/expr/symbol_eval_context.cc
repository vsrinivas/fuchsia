// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/expr/resolve_collection.h"
#include "garnet/bin/zxdb/expr/resolve_ptr_ref.h"
#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/input_location.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/process_symbols.h"
#include "garnet/bin/zxdb/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

Err GetNoVariableErr(const std::string& name) {
  return Err("No variable '%s' in this context.", name.c_str());
}

}  // namespace

SymbolEvalContext::SymbolEvalContext(
    fxl::WeakPtr<const ProcessSymbols> process_symbols, const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider,
    fxl::RefPtr<CodeBlock> code_block)
    : process_symbols_(std::move(process_symbols)),
      symbol_context_(symbol_context),
      data_provider_(data_provider),
      resolver_(std::move(data_provider)),
      block_(std::move(code_block)),
      weak_factory_(this) {}

SymbolEvalContext::SymbolEvalContext(
    fxl::WeakPtr<const ProcessSymbols> process_symbols,
    fxl::RefPtr<SymbolDataProvider> data_provider, const Location& location)
    : process_symbols_(std::move(process_symbols)),
      symbol_context_(location.symbol_context()),
      data_provider_(data_provider),
      resolver_(std::move(data_provider)),
      weak_factory_(this) {
  if (!location.symbol())
    return;
  const CodeBlock* function = location.symbol().Get()->AsCodeBlock();
  if (!function)
    return;

  // Const cast unfortunately required for RefPtr constructor.
  block_ = fxl::RefPtr<const CodeBlock>(
      const_cast<CodeBlock*>(function->GetMostSpecificChild(
          location.symbol_context(), location.address())));
}

SymbolEvalContext::~SymbolEvalContext() = default;

// This function has three phases. (1) search locals, (2) search on |this|, (3)
// search globals and file statics. Phase 2 is asynchronous since it could mean
// resolving the |this| pointer which complicates the flow.
void SymbolEvalContext::GetNamedValue(const std::string& name,
                                      ValueCallback cb) {
  // Search for local variables and function parameters.
  if (const Variable* var = GetLocalVariable(name)) {
    DoResolve(var, std::move(cb));
    return;
  }

  // Otherwise try to resolve the name as a member of the function's |this|
  // pointer.
  //
  // The SymbolVariableResolver has the property that its callbacks are not
  // issued if it is destroyed. Since our class owns the resolver, getting
  // a callback guarantees this is still in scope so it can be captured here.
  GetImplictVariableOnThis(name, [ this, name, cb = std::move(cb) ](
                                     SearchResult result, ExprValue value) {
    switch (result.status) {
      case SearchResult::kFound:
      case SearchResult::kError:
        // Got result or error, forward to our callback.
        cb(result.err, std::move(result.symbol), std::move(value));
        return;
      case SearchResult::kNotFound:
        // Not found on |this|, fall back to searching global vars.
        GetNamedValueFromGlobals(name, std::move(cb));
        break;
    }
  });
}

SymbolVariableResolver& SymbolEvalContext::GetVariableResolver() {
  return resolver_;
}

fxl::RefPtr<SymbolDataProvider> SymbolEvalContext::GetDataProvider() {
  return data_provider_;
}

const Variable* SymbolEvalContext::GetLocalVariable(
    const std::string& name) const {
  // Search backwards in the nested lexical scopes searching for the first
  // variable or function parameter with the given name.
  //
  // This just searches code blocks, it doesn't go higher into containers,
  // namespaces, or compilation units. The container members will be checked
  // by GetNamedValue which calls this function, since they often require
  // resolving the "this" pointer.
  //
  // Variables in the current namespace and compilation unit could be checked
  // in this loop but aren't.
  //
  //  - We want to check globals after members on |this|, but locals before.
  //
  //  - The structure of the symbols doesn't necessarily match the lexical
  //    meaning of the code: there could be multiple declarations of the same
  //    namespace so the containing one doesn't necessarily have the value, and
  //    the storage for the variable is often outside the namespace which means
  //    we have to search for it anyway.
  //
  // As a result, checking namespaces and compilation units in this loop would
  // miss things. Therefore, we go through the index for all global and file
  // static variables so there is one consistent code path. That happens in the
  // block further down in this function.
  const CodeBlock* cur_block = block_.get();
  while (cur_block) {
    if (auto* var = SearchVariableVector(cur_block->variables(), name))
      return var;

    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (auto* var = SearchVariableVector(function->parameters(), name))
        return var;
      break;  // Don't recurse into higher levels of nesting than a function.
    }
    if (!cur_block->parent())
      break;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }
  return nullptr;
}

void SymbolEvalContext::GetImplictVariableOnThis(
    const std::string& name,
    std::function<void(SearchResult, ExprValue)> cb) const {
  if (!block_) {
    // No block means no |this| variable.
    cb(SearchResult(SearchResult::kNotFound), ExprValue());
    return;
  }

  // Find the function to see if it has a |this| pointer.
  const Function* function = block_->GetContainingFunction();
  if (!function || !function->object_pointer()) {
    // No |this| pointer.
    cb(SearchResult(SearchResult::kNotFound), ExprValue());
    return;
  }

  const Variable* this_var = function->object_pointer().Get()->AsVariable();
  if (!this_var) {
    // Symbols corrupt, ignore the |this| pointer.
    cb(SearchResult(SearchResult::kNotFound), ExprValue());
    return;
  }

  // TODO(brettw) this should be synchronous. We know the type of |this| so
  // can synchronously look up if there are any matches. Then we only do the
  // asynchronous lookup if that succeeds. This should simplify this function
  // and make it faster for lookup of global variables.

  // Get the value of of the |this| variable. Callback needs to capture a ref
  // to ourselves since it's needed to resolve the member later.
  fxl::RefPtr<SymbolEvalContext> eval_context(
      const_cast<SymbolEvalContext*>(this));
  resolver_.ResolveVariable(symbol_context_, this_var, [
    cb = std::move(cb), name, eval_context = std::move(eval_context)
  ](const Err& err, ExprValue value) {
    if (err.has_error()) {
      // |this| not available, probably optimized out.
      cb(SearchResult(SearchResult::kError, err), ExprValue());
      return;
    }

    // Got |this|, resolve |this->name|.
    ResolveMemberByPointer(std::move(eval_context), value, name, [
      name, cb = std::move(cb)
    ](const Err& err, fxl::RefPtr<DataMember> symbol, ExprValue value) {
      if (err.has_error()) {
        // Can't resolve the variable on |this|. Drop the input error
        // and report that the variable is not found. Otherwise all
        // unknown variable errors will become "can't resolve on <base
        // class>" which is confusing.
        cb(SearchResult(SearchResult::kNotFound), ExprValue());
      } else {
        // Found |this->name|.
        cb(SearchResult(SearchResult::kFound, Err(), std::move(symbol)),
           std::move(value));
      }
    });
  });
}

// This function is the last step of resolution and takes ownership of the
// callback, so all return paths should issue the callback.
void SymbolEvalContext::GetNamedValueFromGlobals(const std::string& name,
                                                 ValueCallback cb) const {
  if (!process_symbols_) {
    cb(GetNoVariableErr(name), nullptr, ExprValue());
    return;
  }

  std::vector<Location> matches =
      process_symbols_->ResolveInputLocation(InputLocation(name));
  SearchResult result = SearchMatchingLocations(matches, block_.get());
  switch (result.status) {
    case SearchResult::kFound: {
      // Evaluate the value of the variable.
      const Variable* variable = result.symbol->AsVariable();
      if (!variable) {
        cb(Err("Unexpected global variable type."), nullptr,
           ExprValue());
        return;
      }
      DoResolve(variable, std::move(cb));
      break;
    }
    case SearchResult::kError: {
      cb(result.err, std::move(result.symbol), ExprValue());
      break;
    }
    case SearchResult::kNotFound: {
      cb(GetNoVariableErr(name), nullptr, ExprValue());
      break;
    }
  }
}

const Variable* SymbolEvalContext::SearchVariableVector(
    const std::vector<LazySymbol>& vect, const std::string& search_for) const {
  for (const auto& cur : vect) {
    const Variable* var = cur.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    if (search_for == var->GetAssignedName())
      return var;
  }
  return nullptr;
}

void SymbolEvalContext::DoResolve(const Variable* variable,
                                  ValueCallback cb) const {
  fxl::RefPtr<Symbol> sym_ref(const_cast<Variable*>(variable));
  resolver_.ResolveVariable(symbol_context_, variable, [
    cb = std::move(cb), sym_ref = std::move(sym_ref)
  ](const Err& err, ExprValue value) {
    cb(err, std::move(sym_ref), std::move(value));
  });
}

SymbolEvalContext::SearchResult SymbolEvalContext::SearchMatchingLocations(
    const std::vector<Location>& locations, const CodeBlock* source) {
  // TODO(brettw) implement this.
  // TODO(DX-706) search namespaces that match that of the source code block.
  return SearchResult();
}

}  // namespace zxdb
