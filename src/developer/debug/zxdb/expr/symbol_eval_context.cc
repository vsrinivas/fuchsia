// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/symbol_eval_context.h"

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {
namespace {

debug_ipc::RegisterID GetRegister(const ParsedIdentifier& ident) {
  auto str = GetSingleComponentIdentifierName(ident);
  if (!str)
    return debug_ipc::RegisterID::kUnknown;
  return debug_ipc::StringToRegisterID(*str);
}

}  // namespace

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

void SymbolEvalContext::GetNamedValue(const ParsedIdentifier& identifier,
                                      ValueCallback cb) {
  if (FoundName found =
          FindName(GetFindNameContext(),
                   FindNameOptions(FindNameOptions::kAllKinds), identifier)) {
    switch (found.kind()) {
      case FoundName::kVariable:
      case FoundName::kMemberVariable:
        DoResolve(std::move(found), std::move(cb));
        return;
      case FoundName::kNamespace:
        cb(Err("Can not evaluate a namespace."), nullptr, ExprValue());
        return;
      case FoundName::kTemplate:
        cb(Err("Can not evaluate a template with no parameters."), nullptr,
           ExprValue());
        return;
      case FoundName::kType:
        cb(Err("Can not evaluate a type."), nullptr, ExprValue());
        return;
      case FoundName::kFunction:
        break;  // Function pointers not supported yet.
      case FoundName::kNone:
        break;  // Fall through to checking other stuff.
    }
  }

  auto reg = GetRegister(identifier);

  if (reg == debug_ipc::RegisterID::kUnknown ||
      GetArchForRegisterID(reg) != data_provider_->GetArch()) {
    cb(Err("No variable '%s' found.", identifier.GetFullName().c_str()),
       nullptr, ExprValue());
    return;
  }

  // Fall back to matching registers when no symbol is found.
  data_provider_->GetRegisterAsync(
      reg, [cb = std::move(cb)](const Err& err, uint64_t value) {
        cb(err, fxl::RefPtr<zxdb::Symbol>(), ExprValue(value));
      });
}

SymbolVariableResolver& SymbolEvalContext::GetVariableResolver() {
  return resolver_;
}

fxl::RefPtr<SymbolDataProvider> SymbolEvalContext::GetDataProvider() {
  return data_provider_;
}

NameLookupCallback SymbolEvalContext::GetSymbolNameLookupCallback() {
  // The contract for this function is that the callback must not be stored
  // so the callback can reference |this|.
  return [this](const ParsedIdentifier& ident,
                const FindNameOptions& opts) -> FoundName {
    // Look up the symbols in the symbol table if possible.
    FoundName result = FindName(GetFindNameContext(), opts, ident);

    // Fall back on builtin types.
    if (result.kind() == FoundName::kNone && opts.find_types) {
      if (auto type = GetBuiltinType(ident.GetFullName()))
        return FoundName(std::move(type));
    }
    return result;
  };
}

void SymbolEvalContext::DoResolve(FoundName found, ValueCallback cb) const {
  if (found.kind() == FoundName::kVariable) {
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

FoundName SymbolEvalContext::DoTargetSymbolsNameLookup(
    const ParsedIdentifier& ident) {
  return FindName(GetFindNameContext(),
                  FindNameOptions(FindNameOptions::kAllKinds), ident);
}

FindNameContext SymbolEvalContext::GetFindNameContext() {
  return FindNameContext(process_symbols_.get(), symbol_context_, block_.get());
}

}  // namespace zxdb
