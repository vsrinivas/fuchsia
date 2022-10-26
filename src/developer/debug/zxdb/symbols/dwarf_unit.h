// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_

#include <string>

#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class LineTable;

// Represents a DWARF unit in the binary file. The primary purpose of this class is to allow
// mocking the LLVM libraries. It corresponds to what we need from llvm::DWARFUnit and is consumed
// primarily by ModuleSymbolsImpl which provides the high-level symbol interface.
//
// This is a toplevel DWARF concept that contains all the different data types associated with the
// compilation unit like line tables and abbreviation tables. The main thing it contains is the
// Debug Information Entry (DIE) tree for the unit. This tree uses a root of DW_TAG_compilation_unit
// and is represented in our system by the CompileUnit class.
//
// These are similarly named so can be confusing, but this class is the higher-level construct.
// Usually when something ion DWARF is "relative to the compilation unit" it means this class and
// not the CompileUnit DIE.
class DwarfUnit : public fxl::RefCountedThreadSafe<DwarfUnit> {
 public:
  // Creates a weak pointer to this class. The units can get removed when modules or process are
  // unloaded so if you need to keep a pointer, either keep a weak ptr or an owning refptr.
  fxl::WeakPtr<DwarfUnit> GetWeakPtr() const { return weak_factory_.GetWeakPtr(); }

  // Returns the DIE offset, if possible, for the function covering the given absolute/relative
  // address. This will the most specific inlined subroutine if there are any. Returns 0 on failure.
  uint64_t FunctionDieOffsetForAddress(const SymbolContext& symbol_context,
                                       TargetPointer absolute_address) const {
    return FunctionDieOffsetForRelativeAddress(symbol_context.AbsoluteToRelative(absolute_address));
  }
  virtual uint64_t FunctionDieOffsetForRelativeAddress(uint64_t relative_address) const = 0;

  // Returns the offset of the beginning of this unit within the symbol file. Returns 0 on failure.
  // The only failure case is that the symbols were unloaded.
  virtual uint64_t GetOffset() const = 0;

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

  DwarfUnit() : weak_factory_(this) {}
  virtual ~DwarfUnit() = default;

  // For line table back-pointers.
  mutable fxl::WeakPtrFactory<DwarfUnit> weak_factory_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_H_
