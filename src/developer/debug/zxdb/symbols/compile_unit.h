// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_

#include "src/developer/debug/zxdb/symbols/dwarf_lang.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ModuleSymbols;

// Represents a DWARF "unit" DIE. See also DwarfUnit.
class CompileUnit final : public Symbol {
 public:
  // Symbol overrides.
  const CompileUnit* AsCompileUnit() const override { return this; }

  // Module. This can be null if the module was unloaded while somebody held onto this symbol. It
  // is also null in many unit testing situations where mock symbols are created.
  const fxl::WeakPtr<ModuleSymbols>& module() const { return module_; }

  DwarfLang language() const { return language_; }

  // The file name that generated this unit.
  const std::string& name() const { return name_; }

  // Compilation units have a lot of other stuff which we currently have no need for. These can be
  // added here as needed.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CompileUnit);
  FRIEND_MAKE_REF_COUNTED(CompileUnit);

  explicit CompileUnit(fxl::WeakPtr<ModuleSymbols> module, DwarfLang lang, std::string name);
  ~CompileUnit() override;

  fxl::WeakPtr<ModuleSymbols> module_;

  DwarfLang language_ = DwarfLang::kNone;
  std::string name_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_
