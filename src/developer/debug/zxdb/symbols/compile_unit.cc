// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/compile_unit.h"

namespace zxdb {

CompileUnit::CompileUnit(fxl::WeakPtr<ModuleSymbols> module, DwarfLang lang, std::string name)
    : Symbol(DwarfTag::kCompileUnit),
      module_(std::move(module)),
      language_(lang),
      name_(std::move(name)) {}

CompileUnit::~CompileUnit() = default;

}  // namespace zxdb
