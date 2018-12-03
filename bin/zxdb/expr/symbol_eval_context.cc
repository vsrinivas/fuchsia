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

SymbolEvalContext::SymbolEvalContext(
    fxl::WeakPtr<const ProcessSymbols> process_symbols,
    const SymbolContext& symbol_context,
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
  std::optional<FoundVariable> found = FindLocalVariable(name);

  // Search the "this" object.
  if (!found && block_)
    found = FindMemberOnThis(block_.get(), name);

  // Fall back to searching global vars.
  // TODO(brettw) implement this.
  // if (!found)
  //   found = FindGlobalVariable(name);

  if (found)
    DoResolve(std::move(*found), std::move(cb));
  else
    cb(Err("No variable '%s' found.", name.c_str()), nullptr, ExprValue());
}

SymbolVariableResolver& SymbolEvalContext::GetVariableResolver() {
  return resolver_;
}

fxl::RefPtr<SymbolDataProvider> SymbolEvalContext::GetDataProvider() {
  return data_provider_;
}

std::optional<FoundVariable> SymbolEvalContext::FindLocalVariable(
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
      return FoundVariable(var);

    if (const Function* function = cur_block->AsFunction()) {
      // Found a function, check for a match in its parameters.
      if (auto* var = SearchVariableVector(function->parameters(), name))
        return FoundVariable(var);
      break;  // Don't recurse into higher levels of nesting than a function.
    }
    if (!cur_block->parent())
      break;
    cur_block = cur_block->parent().Get()->AsCodeBlock();
  }
  return std::nullopt;
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

void SymbolEvalContext::DoResolve(FoundVariable found, ValueCallback cb) const {
  if (!found.is_object_member()) {
    // Simple variable resolution.
    resolver_.ResolveVariable(symbol_context_, found.variable(),
                              [var = found.variable_ref(), cb = std::move(cb)](
                                  const Err& err, ExprValue value) {
                                cb(err, std::move(var), std::move(value));
                              });
    return;
  }

  // Object variable resolution: Get the value of of the |this| variable.
  // Callback needs to capture a ref to ourselves since it's needed to resolve
  // the member later.
  fxl::RefPtr<SymbolEvalContext> eval_context(
      const_cast<SymbolEvalContext*>(this));
  resolver_.ResolveVariable(
      symbol_context_, found.object_ptr(),
      [found, cb = std::move(cb), eval_context = std::move(eval_context)](
          const Err& err, ExprValue value) {
        if (err.has_error()) {
          // |this| not available, probably optimized out.
          cb(err, fxl::RefPtr<zxdb::Symbol>(), ExprValue());
          return;
        }

        // Got |this|, resolve |this-><DataMember>|.
        ResolveMemberByPointer(
            std::move(eval_context), value, found.member(),
            [found = std::move(found), cb = std::move(cb)](const Err& err,
                                                           ExprValue value) {
              if (err.has_error()) {
                cb(err, fxl::RefPtr<zxdb::Symbol>(), ExprValue());
              } else {
                // Found |this->name|.
                cb(Err(), found.member().data_member_ref(), std::move(value));
              }
            });
      });
}

}  // namespace zxdb
