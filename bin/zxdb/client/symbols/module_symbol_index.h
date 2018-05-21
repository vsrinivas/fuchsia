// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/module_symbol_index_node.h"
#include "garnet/public/lib/fxl/macros.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"

namespace llvm {
class DWARFCompileUnit;
class DWARFContext;
class DWARFDie;
}  // namespace llvm

namespace zxdb {

// Holds the index of symbols for a given module.
class ModuleSymbolIndex {
 public:
  ModuleSymbolIndex();
  ~ModuleSymbolIndex();

  void CreateIndex(llvm::DWARFContext* context,
                   llvm::DWARFUnitSection<llvm::DWARFCompileUnit>& units);

  const ModuleSymbolIndexNode& root() const { return root_; }

  // Takes a fully-qualified name with namespaces and classes and template
  // parameters and returns the list of symbols which match exactly.
  const std::vector<llvm::DWARFDie>& FindFunctionExact(
      const std::string& input) const;

 private:
  void IndexCompileUnit(llvm::DWARFContext* context,
                        llvm::DWARFCompileUnit* unit);

  ModuleSymbolIndexNode root_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbolIndex);
};

}  // namespace zxdb
