// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_H_

#include "src/developer/debug/zxdb/symbols/elf_symbol_record.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ModuleSymbols;

// An ElfSymbol is a symbol read from the ELF file. It does not come from DWARF so has no parent.
class ElfSymbol : public Symbol {
 public:
  // Construct via fxl::MakeRefCounted.

  ElfSymbolType elf_type() const { return record_.type; }

  // The linkage name is the raw name from the ELF file. For C++ programs this will be "mangled".
  // The "full name" and "identifier" will be unmangled if possible.
  const std::string& linkage_name() const { return record_.linkage_name; }

  uint64_t relative_address() const { return record_.relative_address; }

  // Symbol public overrides:
  const ElfSymbol* AsElfSymbol() const override { return this; }
  const std::string& GetAssignedName() const override { return record_.linkage_name; }
  fxl::WeakPtr<ModuleSymbols> GetModuleSymbols() const override { return module_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ElfSymbol);
  FRIEND_MAKE_REF_COUNTED(ElfSymbol);

  ElfSymbol(fxl::WeakPtr<ModuleSymbols> module, const ElfSymbolRecord& record)
      : module_(module), record_(record) {}
  virtual ~ElfSymbol() = default;

  // Symbol protected overrides:
  std::string ComputeFullName() const override;
  Identifier ComputeIdentifier() const override;

  fxl::WeakPtr<ModuleSymbols> module_;

  ElfSymbolRecord record_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_ELF_SYMBOL_H_
