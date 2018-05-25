// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/symbols/module_symbol_index.h"
#include "garnet/public/lib/fxl/macros.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"

namespace llvm {

class DWARFCompileUnit;
class DWARFContext;
class MemoryBuffer;

namespace object {
class Binary;
} // namespace object

}  // namespace llvm

namespace zxdb {

// Represents the symbols for a module (executable or shared library).
//
// All addresses in and out of the API of this class are module-relative. This
// way, the symbol information can be shared between multiple processes that
// have mapped the same .so file (often at different addresses). This means
// that callers have to offset addresses when calling into this class, and
// offset them in the opposite way when they get the results.
class ModuleSymbols {
 public:
  // You must call Load before using this class.
  ModuleSymbols(const std::string& name);
  ~ModuleSymbols();

  // Path name to the local symbol file.
  const std::string& name() const { return name_; }

  Err Load();

  // The addresses in the parameter and the returned Location are relative to
  // the module base. The location will be of type kAddress if there is no
  // symbol for this location.
  Location LocationForAddress(uint64_t address) const;

  // Returns the addresses (relative to the base of this module) for the given
  // function name. The function name must be an exact match. The addresses
  // will indicate the start of the function. Since a function implementation
  // can be duplicated more than once, there can be multiple results.
  std::vector<uint64_t> AddressesForFunction(const std::string& name) const;

 private:
  const std::string name_;

  std::unique_ptr<llvm::MemoryBuffer> binary_buffer_;  // Backing for binary_.
  std::unique_ptr<llvm::object::Binary> binary_;
  std::unique_ptr<llvm::DWARFContext> context_;

  llvm::DWARFUnitSection<llvm::DWARFCompileUnit> compile_units_;

  ModuleSymbolIndex index_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModuleSymbols);
};

}  // namespace zxdb
