// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"

#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
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
  if (!location.function())
    return;
  const CodeBlock* function = location.function().Get()->AsCodeBlock();
  if (!function)
    return;

  // Const case unfortunately required for RefPtr constructor.
  block_ = fxl::RefPtr<const CodeBlock>(
      const_cast<CodeBlock*>(function->GetMostSpecificChild(
          location.symbol_context(), location.address())));
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

void SymbolEvalContext::Dereference(
    const ExprValue& value,
    std::function<void(const Err& err, ExprValue value)> cb) {
  if (!value.type()) {
    cb(Err("Can not dereference null type."), ExprValue());
    return;
  }

  // Validate type is a pointer.
  const ModifiedType* modifier_type =
      value.type()->GetConcreteType()->AsModifiedType();
  if (!modifier_type || modifier_type->tag() != Symbol::kTagPointerType) {
    cb(Err(fxl::StringPrintf("Can not dereference type of '%s'.",
                             value.type()->GetFullName().c_str())),
       ExprValue());
    return;
  }

  // Compute the type the result of the expression will be.
  const Type* dest_type = modifier_type->modified().Get()->AsType();
  if (!dest_type) {
    cb(Err("No underlying type for pointer dereference."), ExprValue());
    return;
  }

  // The resolver will convert the address to a value.
  Err err = value.EnsureSizeIs(sizeof(uint64_t));
  if (err.has_error()) {
    cb(err, ExprValue());
  } else {
    resolver_.ResolveFromAddress(
        value.GetAs<uint64_t>(),
        fxl::RefPtr<Type>(const_cast<Type*>(dest_type)), std::move(cb));
  }
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
