// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_context_impl.h"

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/register_info.h"
#include "src/developer/debug/zxdb/common/adapters.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/eval_dwarf_expr.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "src/developer/debug/zxdb/expr/resolve_const_value.h"
#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"
#include "src/developer/debug/zxdb/expr/resolve_type.h"
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
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {
namespace {

using debug::RegisterID;

RegisterID GetRegisterID(const ParsedIdentifier& ident) {
  // Check for explicit register identifier annotation.
  if (ident.components().size() == 1 &&
      ident.components()[0].special() == SpecialIdentifier::kRegister) {
    return debug::StringToRegisterID(ident.components()[0].name());
  }

  // Try to convert the identifier string to a register name.
  auto str = GetSingleComponentIdentifierName(ident);
  if (!str)
    return debug::RegisterID::kUnknown;
  return debug::StringToRegisterID(*str);
}

Err GetUnavailableRegisterErr(RegisterID id) {
  return Err("Register %s unavailable in this context.", debug::RegisterIDToString(id));
}

ErrOrValue RegisterDataToValue(ExprLanguage lang, RegisterID id, VectorRegisterFormat vector_fmt,
                               cpp20::span<const uint8_t> data) {
  const debug::RegisterInfo* info = debug::InfoForRegister(id);
  if (!info)
    return Err("Unknown register");

  ExprValueSource source(id);

  switch (info->format) {
    case debug::RegisterFormat::kGeneral:
    case debug::RegisterFormat::kSpecial: {
      return ExprValue(GetBuiltinUnsignedType(lang, data.size()),
                       std::vector<uint8_t>(data.begin(), data.end()), source);
    }

    case debug::RegisterFormat::kFloat: {
      return ExprValue(GetBuiltinFloatType(lang, data.size()),
                       std::vector<uint8_t>(data.begin(), data.end()), source);
    }

    case debug::RegisterFormat::kVector: {
      return VectorRegisterToValue(id, vector_fmt, std::vector<uint8_t>(data.begin(), data.end()));
    }

    case debug::RegisterFormat::kVoidAddress: {
      // A void* is a pointer to no type.
      return ExprValue(fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol()),
                       std::vector<uint8_t>(data.begin(), data.end()), source);
    }

    case debug::RegisterFormat::kWordAddress: {
      auto word_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                             GetBuiltinUnsignedType(lang, 8));
      return ExprValue(word_ptr_type, std::vector<uint8_t>(data.begin(), data.end()), source);
    }
  }

  return Err("Unknown register type");
}

}  // namespace

EvalContextImpl::EvalContextImpl(std::shared_ptr<Abi> abi,
                                 fxl::WeakPtr<const ProcessSymbols> process_symbols,
                                 fxl::RefPtr<SymbolDataProvider> data_provider,
                                 ExprLanguage language, fxl::RefPtr<CodeBlock> code_block)
    : abi_(std::move(abi)),
      process_symbols_(std::move(process_symbols)),
      data_provider_(data_provider),
      block_(std::move(code_block)),
      language_(language),
      weak_factory_(this) {}

EvalContextImpl::EvalContextImpl(std::shared_ptr<Abi> abi,
                                 fxl::WeakPtr<const ProcessSymbols> process_symbols,
                                 fxl::RefPtr<SymbolDataProvider> data_provider,
                                 const Location& location,
                                 std::optional<ExprLanguage> force_language)
    : abi_(std::move(abi)),
      process_symbols_(std::move(process_symbols)),
      data_provider_(data_provider),
      weak_factory_(this) {
  const CodeBlock* function = nullptr;
  if (location.symbol())
    function = location.symbol().Get()->As<CodeBlock>();

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

void EvalContextImpl::FindName(const FindNameOptions& options, const ParsedIdentifier& looking_for,
                               std::vector<FoundName>* results) const {
  ::zxdb::FindName(GetFindNameContext(), options, looking_for, results);
}

FindNameContext EvalContextImpl::GetFindNameContext() const {
  // The symbol context for the current location is passed to the FindNameContext to prioritize
  // the current module's values when searching for variables. If relative, this will be ignored.
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  if (block_ && process_symbols_)
    symbol_context = block_->GetSymbolContext(process_symbols_.get());
  return FindNameContext(process_symbols_.get(), symbol_context, block_.get(), language_);
}

void EvalContextImpl::GetNamedValue(const ParsedIdentifier& identifier, EvalCallback cb) const {
  if (FoundName found = FindName(FindNameOptions(FindNameOptions::kAllKinds), identifier)) {
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
      case FoundName::kOtherSymbol:
        break;  // Function pointers not supported yet.
      case FoundName::kNone:
        break;  // Fall through to checking other stuff.
    }
  }

  auto reg = GetRegisterID(identifier);
  if (reg == RegisterID::kUnknown || debug::GetArchForRegisterID(reg) != data_provider_->GetArch())
    return cb(Err("No variable '%s' found.", identifier.GetFullName().c_str()));

  // Fall back to matching registers when no symbol is found. The data_provider is in charge
  // of extracting the bits for non-canonical sub registers (like "ah" and "al" on x86) so we
  // can pass the register enums through directly.
  if (std::optional<cpp20::span<const uint8_t>> opt_reg_data = data_provider_->GetRegister(reg)) {
    // Available synchronously.
    if (opt_reg_data->empty())
      cb(GetUnavailableRegisterErr(reg));
    else
      cb(RegisterDataToValue(language_, reg, GetVectorRegisterFormat(), *opt_reg_data));
  } else {
    data_provider_->GetRegisterAsync(
        reg, [lang = language_, reg, vector_fmt = GetVectorRegisterFormat(), cb = std::move(cb)](
                 const Err& err, std::vector<uint8_t> value) mutable {
          if (err.has_error()) {
            cb(err);
          } else if (value.empty()) {
            cb(GetUnavailableRegisterErr(reg));
          } else {
            cb(RegisterDataToValue(lang, reg, vector_fmt, value));
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
    var = RefPtrTo(input_val->As<Variable>());
    FX_DCHECK(var);
  }

  SymbolContext symbol_context = var->GetSymbolContext(process_symbols_.get());

  // Need to explicitly take a reference to the type.
  fxl::RefPtr<Type> type = RefPtrTo(var->type().Get()->As<Type>());
  if (!type)
    return cb(Err("Missing type information."));

  std::optional<cpp20::span<const uint8_t>> ip_data = data_provider_->GetRegister(
      debug::GetSpecialRegisterID(data_provider_->GetArch(), debug::SpecialRegisterType::kIP));
  TargetPointer ip;
  if (!ip_data || ip_data->size() != sizeof(ip))  // The IP should never require an async call.
    return cb(Err("No location available."));
  memcpy(&ip, &(*ip_data)[0], ip_data->size());

  const DwarfExpr* loc_expr = var->location().ExprForIP(symbol_context, ip);
  if (!loc_expr) {
    // No DWARF location applies to the current instruction pointer.
    const char* err_str;
    if (var->location().is_null()) {
      // With no locations, this variable has been completely optimized out.
      err_str = "Optimized out";
    } else {
      // There are locations but none of them match the current IP.
      err_str = "Unavailable";
    }
    return cb(Err(ErrType::kOptimizedOut, err_str));
  }

  // Schedule the expression to be evaluated.
  DwarfExprToValue(UnitSymbolFactory(input_val.get()), RefPtrTo(this), symbol_context, *loc_expr,
                   std::move(type), std::move(cb));
}

const ProcessSymbols* EvalContextImpl::GetProcessSymbols() const {
  if (!process_symbols_)
    return nullptr;
  return process_symbols_.get();
}

fxl::RefPtr<SymbolDataProvider> EvalContextImpl::GetDataProvider() { return data_provider_; }

Location EvalContextImpl::GetLocationForAddress(uint64_t address) const {
  if (!process_symbols_)
    return Location(Location::State::kAddress, address);  // Can't symbolize.

  auto locations = process_symbols_->ResolveInputLocation(InputLocation(address));

  // Given an exact address, ResolveInputLocation() should only return one result.
  FX_DCHECK(locations.size() == 1u);
  return locations[0];
}

Err EvalContextImpl::ResolveExternValue(const fxl::RefPtr<Value>& input_value,
                                        fxl::RefPtr<Variable>* resolved) const {
  FX_DCHECK(input_value->is_external());

  FindNameOptions options(FindNameOptions::kNoKinds);
  options.find_vars = true;

  // Passing a null block in the FindNameContext will bypass searching the current scope and
  // "this" object and instead only search global names. This is what we want since the extern
  // Value name will be fully qualified.
  FindNameContext context = GetFindNameContext();
  context.block = nullptr;

  // This call into the toplevel FindName() bypasses any mocking on the eval context because we need
  // to supply our own context. We could have the virtual EvalContext::FindName take a context to
  // avoid this, but extern values won't currently be generated for these mock values so we won't
  // get here in the first place.
  FoundName found =
      ::zxdb::FindName(context, options, ToParsedIdentifier(input_value->GetIdentifier()));
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
  FX_DCHECK(found.kind() == FoundName::kMemberVariable);

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
  return FindName(FindNameOptions(FindNameOptions::kAllKinds), ident);
}

}  // namespace zxdb
