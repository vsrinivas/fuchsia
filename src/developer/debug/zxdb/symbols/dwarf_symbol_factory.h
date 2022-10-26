// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_SYMBOL_FACTORY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_SYMBOL_FACTORY_H_

#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"
#include "src/developer/debug/zxdb/symbols/symbol_factory.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace llvm {
class DWARFContext;
class DWARFDie;
}  // namespace llvm

namespace zxdb {

class BaseType;
class LazySymbol;
class ModuleSymbolsImpl;
class UncachedLazySymbol;

// Implementation of SymbolFactory that reads from the DWARF symbols in the given module.
class DwarfSymbolFactory : public SymbolFactory {
 public:
  // SymbolFactory implementation.
  fxl::RefPtr<Symbol> CreateSymbol(uint64_t die_offset) const override;

  using SymbolFactory::MakeLazy;
  using SymbolFactory::MakeUncachedLazy;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DwarfSymbolFactory);
  FRIEND_MAKE_REF_COUNTED(DwarfSymbolFactory);

  explicit DwarfSymbolFactory(fxl::WeakPtr<ModuleSymbolsImpl> symbols);
  ~DwarfSymbolFactory() override;

  LazySymbol MakeLazy(const llvm::DWARFDie& die) const;
  UncachedLazySymbol MakeUncachedLazy(const llvm::DWARFDie& die) const;

  llvm::DWARFContext* GetLLVMContext() const;

  // Internal version that creates a symbol from a Die.
  fxl::RefPtr<Symbol> DecodeSymbol(const llvm::DWARFDie& die) const;

  // As with SymbolFactory::CreateSymbol, these should never return null but rather an empty Symbol
  // implementation on error.
  //
  // is_specification will be set when this function recursively calls itself to parse the
  // specification of a function implementation.
  //
  // The tag (DW_TAG_subprogram or DW_TAG_inlined_subroutine) is passed in because when recursively
  // looking up the definitions, we want the original DIE tag rather than the specification's tag
  // (the original could be an inlined function while the specification will never be).
  fxl::RefPtr<Symbol> DecodeFunction(const llvm::DWARFDie& die, DwarfTag tag,
                                     bool is_specification = false) const;
  fxl::RefPtr<Symbol> DecodeArrayType(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeBaseType(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeCallSite(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeCallSiteParameter(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeCollection(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeCompileUnit(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeDataMember(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeEnum(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeFunctionType(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeImportedDeclaration(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeInheritedFrom(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeLexicalBlock(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeMemberPtr(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeModifiedType(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeNamespace(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeTemplateParameter(const llvm::DWARFDie& die, DwarfTag tag) const;
  fxl::RefPtr<Symbol> DecodeUnspecifiedType(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeVariable(const llvm::DWARFDie& die,
                                     bool is_specification = false) const;
  fxl::RefPtr<Symbol> DecodeVariant(const llvm::DWARFDie& die) const;
  fxl::RefPtr<Symbol> DecodeVariantPart(const llvm::DWARFDie& die) const;

  // This can be null if the module is unloaded but there are still some dangling type references to
  // it.
  fxl::WeakPtr<ModuleSymbolsImpl> symbols_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_SYMBOL_FACTORY_H_
