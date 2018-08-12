// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"

#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

SymbolEvalContext::SymbolEvalContext(
    const SymbolContext& symbol_context,
    fxl::RefPtr<SymbolDataProvider> data_provider,
    fxl::RefPtr<CodeBlock> code_block)
    : symbol_context_(symbol_context),
      resolver_(std::move(data_provider)),
      block_(std::move(code_block)),
      weak_factory_(this) {}

SymbolEvalContext::SymbolEvalContext(fxl::RefPtr<SymbolDataProvider> data_provider,
                                     const Location& location)
    : symbol_context_(location.symbol_context()),
      resolver_(std::move(data_provider)),
      weak_factory_(this) {
  if (!location.function())
    return;
  const CodeBlock* function = location.function().Get()->AsCodeBlock();
  if (!function)
    return;

  // Const case unfortunately required for RefPtr constructor.
  block_ = fxl::RefPtr<const CodeBlock>(const_cast<CodeBlock*>(
      function->GetMostSpecificChild(location.address())));
}

void SymbolEvalContext::GetVariable(const std::string& name, Callback cb) {
  // Search backwards in the nested lexical scopes searching for the first
  // variable or function parameter with the given name.
  const CodeBlock* cur_block = block_.get();
  while (cur_block) {
    if (SearchVariableVector(cur_block->variables(), name, cb))
      return;

    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (SearchVariableVector(function->parameters(), name, cb))
        return;
      break;  // Don't recurse into higher levels of nesting than a function.
    }
    if (!cur_block->parent())
      break;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }

  // Not found. In the future, it might be nice to suggest the closest
  // match in the error message.
  cb(Err(fxl::StringPrintf("No variable '%s' in this context", name.c_str())),
     ExprValue());
}

bool SymbolEvalContext::SearchVariableVector(
    const std::vector<LazySymbol>& vect, const std::string& search_for,
    Callback& cb) {
  for (const auto& cur : vect) {
    const Variable* var = cur.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    if (search_for == var->GetAssignedName()) {
      resolver_.ResolveVariable(symbol_context_, var, std::move(cb));
      return true;
    }
  }
  return false;
}

}  // namespace zxdb
