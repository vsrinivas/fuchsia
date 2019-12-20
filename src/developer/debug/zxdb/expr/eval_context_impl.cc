// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_context_impl.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/adapters.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/async_dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_const_value.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
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

using debug_ipc::RegisterID;

RegisterID GetRegisterID(const ParsedIdentifier& ident) {
  auto str = GetSingleComponentIdentifierName(ident);
  if (!str)
    return debug_ipc::RegisterID::kUnknown;
  if (!str->empty() && (*str)[0] == '$') {
    return debug_ipc::StringToRegisterID(str->substr(1));
  }
  return debug_ipc::StringToRegisterID(*str);
}

Err GetUnavailableRegisterErr(RegisterID id) {
  return Err("Register %s unavailable in this context.", debug_ipc::RegisterIDToString(id));
}

ErrOrValue RegisterDataToValue(RegisterID id, VectorRegisterFormat vector_fmt,
                               containers::array_view<uint8_t> data) {
  if (ShouldFormatRegisterAsVector(id))
    return VectorRegisterToValue(id, vector_fmt, std::vector<uint8_t>(data.begin(), data.end()));

  ExprValueSource source(id);

  if (data.size() <= sizeof(uint64_t)) {
    uint64_t int_value = 0;
    memcpy(&int_value, &data[0], data.size());

    // Use the types defined by ExprValue for the unsigned number of the corresponding size.
    // Passing a null type will cause ExprValue to create one matching the input type.
    switch (data.size()) {
      case 1:
        return ExprValue(static_cast<uint8_t>(int_value), fxl::RefPtr<Type>(), source);
      case 2:
        return ExprValue(static_cast<uint16_t>(int_value), fxl::RefPtr<Type>(), source);
      case 4:
        return ExprValue(static_cast<uint32_t>(int_value), fxl::RefPtr<Type>(), source);
      case 8:
        return ExprValue(static_cast<uint64_t>(int_value), fxl::RefPtr<Type>(), source);
    }
  }

  // Large and/or weird sized registers.
  const char* type_name;
  if (data.size() == sizeof(uint128_t))
    type_name = "uint128_t";
  else
    type_name = "(register data)";

  return ExprValue(
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, data.size(), type_name),
      std::vector<uint8_t>(data.begin(), data.end()), source);
}

}  // namespace

EvalContextImpl::EvalContextImpl(fxl::WeakPtr<const ProcessSymbols> process_symbols,
                                 fxl::RefPtr<SymbolDataProvider> data_provider,
                                 ExprLanguage language, fxl::RefPtr<CodeBlock> code_block)
    : process_symbols_(std::move(process_symbols)),
      data_provider_(data_provider),
      block_(std::move(code_block)),
      language_(language),
      weak_factory_(this) {}

EvalContextImpl::EvalContextImpl(fxl::WeakPtr<const ProcessSymbols> process_symbols,
                                 fxl::RefPtr<SymbolDataProvider> data_provider,
                                 const Location& location,
                                 std::optional<ExprLanguage> force_language)
    : process_symbols_(std::move(process_symbols)),
      data_provider_(data_provider),
      weak_factory_(this) {
  const CodeBlock* function = nullptr;
  if (location.symbol())
    function = location.symbol().Get()->AsCodeBlock();

  if (function) {
    block_ =
        RefPtrTo(function->GetMostSpecificChild(location.symbol_context(), location.address()));
  }

  if (force_language) {
    language_ = *force_language;
  } else if (function) {
    // Extract the language for the code if possible.
    if (auto unit = function->GetCompileUnit())
      language_ = DwarfLangToExprLanguage(unit->language());
  }
}

EvalContextImpl::~EvalContextImpl() = default;

ExprLanguage EvalContextImpl::GetLanguage() const { return language_; }

void EvalContextImpl::GetNamedValue(const ParsedIdentifier& identifier, EvalCallback cb) const {
  if (FoundName found =
          FindName(GetFindNameContext(), FindNameOptions(FindNameOptions::kAllKinds), identifier)) {
    switch (found.kind()) {
      case FoundName::kVariable:
      case FoundName::kMemberVariable:
        DoResolve(std::move(found), std::move(cb));
        return;
      case FoundName::kNamespace:
        cb(Err("Can not evaluate a namespace."));
        return;
      case FoundName::kTemplate:
        cb(Err("Can not evaluate a template with no parameters."));
        return;
      case FoundName::kType:
        cb(Err("Can not evaluate a type."));
        return;
      case FoundName::kFunction:
        break;  // Function pointers not supported yet.
      case FoundName::kNone:
        break;  // Fall through to checking other stuff.
    }
  }

  auto reg = GetRegisterID(identifier);
  if (reg == RegisterID::kUnknown || GetArchForRegisterID(reg) != data_provider_->GetArch())
    return cb(Err("No variable '%s' found.", identifier.GetFullName().c_str()));

  // Fall back to matching registers when no symbol is found.
  if (std::optional<containers::array_view<uint8_t>> opt_reg_data =
          data_provider_->GetRegister(reg)) {
    // Available synchronously.
    if (opt_reg_data->empty())
      cb(GetUnavailableRegisterErr(reg));
    else
      cb(RegisterDataToValue(reg, GetVectorRegisterFormat(), *opt_reg_data));
  } else {
    data_provider_->GetRegisterAsync(
        reg, [reg, vector_fmt = GetVectorRegisterFormat(), cb = std::move(cb)](
                 const Err& err, std::vector<uint8_t> value) mutable {
          if (err.has_error()) {
            cb(err);
          } else if (value.empty()) {
            cb(GetUnavailableRegisterErr(reg));
          } else {
            cb(RegisterDataToValue(reg, vector_fmt, value));
          }
        });
  }
}

void EvalContextImpl::GetVariableValue(fxl::RefPtr<Value> input_val, EvalCallback cb) const {
  // Handle const values.
  if (input_val->const_value().has_value())
    return cb(ResolveConstValue(RefPtrTo(this), input_val.get()));

  fxl::RefPtr<Variable> var;
  if (input_val->is_external()) {
    // Convert extern Variables and DataMembers to the actual variable memory.
    if (Err err = ResolveExternValue(input_val, &var); err.has_error())
      return cb(err);
  } else {
    // Everything else should be a variable.
    var = RefPtrTo(input_val->AsVariable());
    FXL_DCHECK(var);
  }

  SymbolContext symbol_context = var->GetSymbolContext(process_symbols_.get());

  // Need to explicitly take a reference to the type.
  fxl::RefPtr<Type> type = RefPtrTo(var->type().Get()->AsType());
  if (!type)
    return cb(Err("Missing type information."));

  std::optional<containers::array_view<uint8_t>> ip_data =
      data_provider_->GetRegister(debug_ipc::GetSpecialRegisterID(
          data_provider_->GetArch(), debug_ipc::SpecialRegisterType::kIP));
  TargetPointer ip;
  if (!ip_data || ip_data->size() != sizeof(ip))  // The IP should never require an async call.
    return cb(Err("No location available."));
  memcpy(&ip, &(*ip_data)[0], ip_data->size());

  const VariableLocation::Entry* loc_entry = var->location().EntryForIP(symbol_context, ip);
  if (!loc_entry) {
    // No DWARF location applies to the current instruction pointer.
    const char* err_str;
    if (var->location().is_null()) {
      // With no locations, this variable has been completely optimized out.
      err_str = "Optimized out.";
    } else {
      // There are locations but none of them match the current IP.
      err_str = "Unavailable";
    }
    return cb(Err(ErrType::kOptimizedOut, err_str));
  }

  // Schedule the expression to be evaluated.
  auto evaluator = fxl::MakeRefCounted<AsyncDwarfExprEval>(std::move(cb), std::move(type));
  evaluator->Eval(RefPtrTo(this), symbol_context, loc_entry->expression);
}

fxl::RefPtr<Type> EvalContextImpl::ResolveForwardDefinition(const Type* type) const {
  Identifier ident = type->GetIdentifier();
  if (ident.empty()) {
    // Some things like modified types don't have real identifier names.
    return RefPtrTo(type);
  }

  fxl::RefPtr<Type> result = ResolveForwardDefinition(ToParsedIdentifier(ident));
  if (result)
    return result;
  return RefPtrTo(type);  // Return the same input on failure.
}

fxl::RefPtr<Type> EvalContextImpl::ResolveForwardDefinition(ParsedIdentifier type_name) const {
  // Search for the first match of a type definition. Note that "find_types" is not desirable here
  // since we only want to resolve real definitions. Normally the index contains only definitions
  // but if a module contains only declarations that module's index will list the symbol as a
  // declaration which we don't want.
  FindNameOptions opts(FindNameOptions::kNoKinds);
  opts.find_type_defs = true;
  opts.max_results = 1;

  // The type names will always be fully qualified. Mark the identifier as
  // such and only search the global context by clearing the code location.
  type_name.set_qualification(IdentifierQualification::kGlobal);
  auto context = GetFindNameContext();
  context.block = nullptr;

  if (FoundName result = FindName(context, opts, type_name)) {
    FXL_DCHECK(result.type());
    return result.type();
  }

  // Nothing found in the index.
  return nullptr;
}

fxl::RefPtr<Type> EvalContextImpl::GetConcreteType(const Type* type) const {
  if (!type)
    return fxl::RefPtr<Type>();

  // Iteratively strip C-V qualifications, follow typedefs, and follow forward
  // declarations.
  fxl::RefPtr<Type> cur = RefPtrTo(type);
  do {
    // Follow forward declarations.
    if (cur->is_declaration()) {
      cur = ResolveForwardDefinition(cur.get());
      if (cur->is_declaration())
        break;  // Declaration can't be resolved, give up.
    }

    // Strip C-V qualifiers and follow typedefs.
    cur = RefPtrTo(cur->StripCVT());
  } while (cur && cur->is_declaration());
  return cur;
}

fxl::RefPtr<SymbolDataProvider> EvalContextImpl::GetDataProvider() { return data_provider_; }

NameLookupCallback EvalContextImpl::GetSymbolNameLookupCallback() {
  // The contract for this function is that the callback must not be stored
  // so the callback can reference |this|.
  return [this](const ParsedIdentifier& ident, const FindNameOptions& opts) -> FoundName {
    // Look up the symbols in the symbol table if possible.
    FoundName result = FindName(GetFindNameContext(), opts, ident);

    // Fall back on builtin types.
    if (result.kind() == FoundName::kNone && opts.find_types) {
      if (auto type = GetBuiltinType(language_, ident.GetFullName()))
        return FoundName(std::move(type));
    }
    return result;
  };
}

Location EvalContextImpl::GetLocationForAddress(uint64_t address) const {
  if (!process_symbols_)
    return Location(Location::State::kAddress, address);  // Can't symbolize.

  auto locations = process_symbols_->ResolveInputLocation(InputLocation(address));

  // Given an exact address, ResolveInputLocation() should only return one result.
  FXL_DCHECK(locations.size() == 1u);
  return locations[0];
}

Err EvalContextImpl::ResolveExternValue(const fxl::RefPtr<Value>& input_value,
                                        fxl::RefPtr<Variable>* resolved) const {
  FXL_DCHECK(input_value->is_external());

  FindNameOptions options(FindNameOptions::kNoKinds);
  options.find_vars = true;

  // Passing a null block in the FindNameContext will bypass searching the current scope and
  // "this" object and instead only search global names. This is what we want since the extern
  // Value name will be fully qualified.
  FindNameContext context = GetFindNameContext();
  context.block = nullptr;

  FoundName found = FindName(context, options, ToParsedIdentifier(input_value->GetIdentifier()));
  if (!found || !found.variable())
    return Err("Extern variable '%s' not found.", input_value->GetFullName().c_str());

  *resolved = found.variable_ref();
  return Err();
}

void EvalContextImpl::DoResolve(FoundName found, EvalCallback cb) const {
  if (found.kind() == FoundName::kVariable) {
    // Simple variable resolution.
    GetVariableValue(found.variable_ref(), std::move(cb));
    return;
  }

  // Everything below here is an object variable resolution.
  FXL_DCHECK(found.kind() == FoundName::kMemberVariable);

  // Static ("external") data members don't require a "this" pointer.
  if (found.member().data_member()->is_external())
    return GetVariableValue(RefPtrTo(found.member().data_member()), std::move(cb));

  // Get the value of of the |this| variable to resolve.
  GetVariableValue(found.object_ptr_ref(), [weak_this = weak_factory_.GetWeakPtr(), found,
                                            cb = std::move(cb)](ErrOrValue value) mutable {
    if (!weak_this)
      return;  // Don't issue callbacks if we've been destroyed.

    if (value.has_error())  // |this| not available, probably optimized out.
      return cb(value);

    // Got |this|, resolve |this-><DataMember>|.
    //
    // Here we do not support automatically converting a base class pointer to a derived class if
    // we can. First, that's more difficult to implement because it requires asynchronously
    // computing the derived class based on |this|'s vtable pointer. Second, it's not linguistically
    // in scope and it could be surprising, especially if it shadows another value. The user can
    // always do "this->foo" to expicitly request the conversion if enabled.
    ResolveMemberByPointer(fxl::RefPtr<EvalContextImpl>(weak_this.get()), value.value(),
                           found.member(),
                           [weak_this, found, cb = std::move(cb)](ErrOrValue value) mutable {
                             if (weak_this) {
                               // Only issue callbacks if we're still alive.
                               cb(std::move(value));
                             }
                           });
  });
}

FoundName EvalContextImpl::DoTargetSymbolsNameLookup(const ParsedIdentifier& ident) {
  return FindName(GetFindNameContext(), FindNameOptions(FindNameOptions::kAllKinds), ident);
}

FindNameContext EvalContextImpl::GetFindNameContext() const {
  // The synbol context for the current location is passed to the FindNameContext to prioritize
  // the current module's values when searching for variables. If relative, this will be ignored.
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  if (block_ && process_symbols_)
    symbol_context = block_->GetSymbolContext(process_symbols_.get());
  return FindNameContext(process_symbols_.get(), symbol_context, block_.get());
}

}  // namespace zxdb
