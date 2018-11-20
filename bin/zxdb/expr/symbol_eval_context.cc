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
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
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
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider,
    fxl::RefPtr<CodeBlock> code_block)
    : symbol_context_(symbol_context),
      data_provider_(data_provider),
      resolver_(std::move(data_provider)),
      block_(std::move(code_block)),
      weak_factory_(this) {}

SymbolEvalContext::SymbolEvalContext(
    fxl::RefPtr<SymbolDataProvider> data_provider, const Location& location)
    : symbol_context_(location.symbol_context()),
      data_provider_(data_provider),
      resolver_(std::move(data_provider)),
      weak_factory_(this) {
  if (!location.symbol())
    return;
  const CodeBlock* function = location.symbol().Get()->AsCodeBlock();
  if (!function)
    return;

  // Const case unfortunately required for RefPtr constructor.
  block_ = fxl::RefPtr<const CodeBlock>(
      const_cast<CodeBlock*>(function->GetMostSpecificChild(
          location.symbol_context(), location.address())));
}

SymbolEvalContext::~SymbolEvalContext() = default;

void SymbolEvalContext::GetNamedValue(const std::string& name,
                                      ValueCallback cb) {
  // Search for local variables and function parameters.
  if (const Variable* var = GetLocalVariable(name)) {
    // Found local variable, resolve value.
    fxl::RefPtr<Symbol> sym_ref(const_cast<Variable*>(var));
    resolver_.ResolveVariable(symbol_context_, var, [
      cb = std::move(cb), sym_ref = std::move(sym_ref)
    ](const Err& err, ExprValue value) {
      cb(err, std::move(sym_ref), std::move(value));
    });
    return;
  }
  // Otherwise try to resolve the name on the |this| pointer.

  // Find the function to see if it has a |this| pointer.
  const Function* function = block_->GetContainingFunction();
  if (!function || !function->object_pointer()) {
    // No |this| pointer.
    cb(GetNoVariableErr(name), fxl::RefPtr<Symbol>(), ExprValue());
    return;
  }

  const Variable* this_var = function->object_pointer().Get()->AsVariable();
  if (!this_var) {
    // Symbols corrupt.
    cb(GetNoVariableErr(name), fxl::RefPtr<Symbol>(), ExprValue());
    return;
  }

  // Get the value of of the |this| variable. Callback needs to capture a ref
  // to ourselves since it's needed to resolve the member later.
  fxl::RefPtr<SymbolEvalContext> eval_context(this);
  resolver_.ResolveVariable(symbol_context_, this_var, [
    cb = std::move(cb), name, eval_context = std::move(eval_context)
  ](const Err& err, ExprValue value) {
    if (err.has_error()) {
      // |this| not available, probably optimized out.
      cb(err, fxl::RefPtr<Symbol>(), ExprValue());
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
        cb(GetNoVariableErr(name), symbol, ExprValue());
      } else {
        // Found |this->name|.
        cb(Err(), std::move(symbol), std::move(value));
      }
    });
  });
}

SymbolVariableResolver& SymbolEvalContext::GetVariableResolver() {
  return resolver_;
}

fxl::RefPtr<SymbolDataProvider> SymbolEvalContext::GetDataProvider() {
  return data_provider_;
}

const Variable* SymbolEvalContext::GetLocalVariable(const std::string& name) {
  // Search backwards in the nested lexical scopes searching for the first
  // variable or function parameter with the given name.
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

const Variable* SymbolEvalContext::SearchVariableVector(
    const std::vector<LazySymbol>& vect, const std::string& search_for) {
  for (const auto& cur : vect) {
    const Variable* var = cur.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    if (search_for == var->GetAssignedName())
      return var;
  }
  return nullptr;
}

}  // namespace zxdb
