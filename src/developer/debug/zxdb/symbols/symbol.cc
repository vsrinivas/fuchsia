// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol.h"

#include "src/developer/debug/zxdb/common/ref_ptr_to.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/developer/debug/zxdb/symbols/type.h"

namespace zxdb {

Symbol::Symbol() = default;
Symbol::Symbol(DwarfTag tag) : tag_(tag) {}
Symbol::~Symbol() = default;

LazySymbol Symbol::GetLazySymbol() const {
  if (lazy_this_.is_valid())
    return lazy_this_.GetCached(RefPtrTo(this));

  // In this case, this symbol is likely a synthetic symbol (like a built-in type) or something
  // created manually in a unit test. Create a LazySymbol that just holds a reference.
  return LazySymbol(RefPtrTo(this));
}

uint64_t Symbol::GetDieOffset() const {
  if (lazy_this_.is_valid())
    return lazy_this_.die_offset();
  return 0;
}

const std::string& Symbol::GetAssignedName() const {
  const static std::string empty;
  return empty;
}

const std::string& Symbol::GetFullName() const {
  if (!full_name_)
    full_name_ = ComputeFullName();
  return *full_name_;
}

const Identifier& Symbol::GetIdentifier() const {
  if (!identifier_)
    identifier_ = ComputeIdentifier();
  return *identifier_;
}

fxl::RefPtr<CompileUnit> Symbol::GetCompileUnit() const {
  // Currently we don't use compile units very often. This implementation walks up the symbol
  // hierarchy until we find one. This has the disadvantage that it decodes the tree of DIEs up to
  // here which is potentially slow, and if anything fails the path will get lost (even when we can
  // get at the unit via other means).
  //
  // The compile unit is known at the time of decode and we could just stash a pointer on each
  // symbol. This would make them larger, however, and we should take steps to ensure that the unit
  // objects are re-used so we don't get them created all over.
  //
  // Each LazySymbol also has an offset of the compile unit. But symbols don't have a LazySymbol for
  // their *own* symbol. Perhaps they should? In that case we would add a new function to the symbol
  // factory to get the unit for a LazySymbol.
  fxl::RefPtr<Symbol> cur = RefPtrTo(this);
  for (;;) {
    if (const CompileUnit* unit = cur->AsCompileUnit())
      return RefPtrTo(unit);
    if (!cur->parent())
      return fxl::RefPtr<CompileUnit>();
    cur = cur->parent().Get();
  }
}

fxl::RefPtr<DwarfUnit> Symbol::GetDwarfUnit() const {
  fxl::RefPtr<CompileUnit> comp_unit = GetCompileUnit();
  if (!comp_unit)
    return fxl::RefPtr<DwarfUnit>();
  return RefPtrTo(comp_unit->dwarf_unit());
}

fxl::WeakPtr<ModuleSymbols> Symbol::GetModuleSymbols() const {
  fxl::RefPtr<CompileUnit> unit = GetCompileUnit();
  if (!unit)
    return fxl::WeakPtr<ModuleSymbols>();
  return unit->module();
}

SymbolContext Symbol::GetSymbolContext(const ProcessSymbols* process_symbols) const {
  if (!process_symbols)
    return SymbolContext::ForRelativeAddresses();

  fxl::WeakPtr<ModuleSymbols> weak_mod = GetModuleSymbols();
  if (!weak_mod)
    return SymbolContext::ForRelativeAddresses();

  const LoadedModuleSymbols* loaded = process_symbols->GetLoadedForModuleSymbols(weak_mod.get());
  if (!loaded)
    return SymbolContext::ForRelativeAddresses();

  return loaded->symbol_context();
}

DwarfLang Symbol::GetLanguage() const {
  if (auto unit = GetCompileUnit())
    return unit->language();
  return DwarfLang::kNone;
}

const ArrayType* Symbol::AsArrayType() const { return nullptr; }
const BaseType* Symbol::AsBaseType() const { return nullptr; }
const CallSite* Symbol::AsCallSite() const { return nullptr; }
const CallSiteParameter* Symbol::AsCallSiteParameter() const { return nullptr; }
const CodeBlock* Symbol::AsCodeBlock() const { return nullptr; }
const CompileUnit* Symbol::AsCompileUnit() const { return nullptr; }
const Collection* Symbol::AsCollection() const { return nullptr; }
const DataMember* Symbol::AsDataMember() const { return nullptr; }
const ElfSymbol* Symbol::AsElfSymbol() const { return nullptr; }
const Enumeration* Symbol::AsEnumeration() const { return nullptr; }
const Function* Symbol::AsFunction() const { return nullptr; }
const FunctionType* Symbol::AsFunctionType() const { return nullptr; }
const InheritedFrom* Symbol::AsInheritedFrom() const { return nullptr; }
const MemberPtr* Symbol::AsMemberPtr() const { return nullptr; }
const ModifiedType* Symbol::AsModifiedType() const { return nullptr; }
const Namespace* Symbol::AsNamespace() const { return nullptr; }
const TemplateParameter* Symbol::AsTemplateParameter() const { return nullptr; }
const Type* Symbol::AsType() const { return nullptr; }
const Value* Symbol::AsValue() const { return nullptr; }
const Variable* Symbol::AsVariable() const { return nullptr; }
const Variant* Symbol::AsVariant() const { return nullptr; }
const VariantPart* Symbol::AsVariantPart() const { return nullptr; }

std::string Symbol::ComputeFullName() const { return GetIdentifier().GetFullNameNoQual(); }

Identifier Symbol::ComputeIdentifier() const {
  const std::string& assigned_name = GetAssignedName();
  if (assigned_name.empty()) {
    // When a thing doesn't have a name, don't try to qualify it, since returning "foo::" for the
    // name of something like a lexical block is actively confusing.
    return Identifier();
  }

  // This base type class just uses the qualified name for the full name.  Derived classes will
  // override this function to apply modifiers.
  Identifier result = GetSymbolScopePrefix(this);
  result.AppendComponent(IdentifierComponent(assigned_name));
  return result;
}

}  // namespace zxdb
