// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_

#include <string>

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class LineTable;

// Represents a DWARF unit in the binary file. The primary purpose of this class is to allow
// mocking the LLVM libraries. It corresponds to what we need from llvm::DWARFUnit and is consumed
// primarily by ModuleSymbolsImpl which provides the high-level symbol interface.
//
// This is the higher-level thing that contains the line tables and such for the unit. The specific
// DIE for the compilation unit is represented by the CompileUnit class.
class DwarfUnit : public fxl::RefCountedThreadSafe<DwarfUnit> {
 public:
  // Returns the DIE information, if possible, for the function covering the given absolute address.
  // TODO(brettw) return something higher-level than a DWARFDie.
  llvm::DWARFDie FunctionForAddress(const SymbolContext& symbol_context,
                                    TargetPointer absolute_address) const {
    return FunctionForRelativeAddress(symbol_context.AbsoluteToRelative(absolute_address));
  }

  // Returns the DIE information, if possible, for the function covering the given relative address.
  // TODO(brettw) return something higher-level than a DWARFDie.
  virtual llvm::DWARFDie FunctionForRelativeAddress(uint64_t relative_address) const = 0;

  // The compilation directory is what the compiler decides to write. In normal usage this will be
  // an absolute directory on the current computer. In the Fuchsia in-tree build this will be
  // relative.
  virtual std::string GetCompilationDir() const = 0;

  // The line table maps addresses to line numbers.
  virtual const LineTable& GetLineTable() const = 0;

  // Returns the internal LLVM line table. This will be null if there is no line table or there
  // is no LLVM object backing this unit.
  //
  // TODO(brettw) this should be removed and all callers should use GetLineTable() so we don't have
  // to expose the internal LLVM line table pointer. This would allow us to mock the whole line
  // table for symbol tests.
  //
  // The reason this function is here is that some older code uses the LLVM because making our
  // LineTable from LLVM's requires copying the table. This is nontrivial and we don't want to do
  // every time this is called. Therefore, we'd want to cache it on a DwarfUnit. But to be useful
  // the DwarfUnits must themselves be cached in the DwarfBinary whic does not happen yet.
  virtual const llvm::DWARFDebugLine::LineTable* GetLLVMLineTable() const = 0;

 protected:
  FRIEND_REF_COUNTED_THREAD_SAFE(DwarfUnit);

  virtual ~DwarfUnit() = default;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_
