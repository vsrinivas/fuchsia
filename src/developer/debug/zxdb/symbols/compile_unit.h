// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_

#include "src/developer/debug/zxdb/symbols/dwarf_lang.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class CompileUnit final : public Symbol {
 public:
  // Symbol overrides.
  const CompileUnit* AsCompileUnit() const override { return this; }

  DwarfLang language() const { return language_; }
  void set_language(DwarfLang lang) { language_ = lang; }

  // The file name that generated this unit.
  const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = name; }

  // Compilation units have a lot of other stuff which we currently have no need for. These can be
  // added here as needed.

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CompileUnit);
  FRIEND_MAKE_REF_COUNTED(CompileUnit);

  CompileUnit();
  ~CompileUnit() override;

  DwarfLang language_ = DwarfLang::kNone;
  std::string name_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_COMPILE_UNIT_H_
