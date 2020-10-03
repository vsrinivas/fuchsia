// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_BINARY_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_BINARY_H_

#include "llvm/BinaryFormat/ELF.h"
#include "src/developer/debug/zxdb/symbols/dwarf_unit.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"

namespace llvm {

namespace object {
class Binary;
class ObjectFile;
}  // namespace object

}  // namespace llvm

namespace zxdb {

class SymbolContext;

// Represents the low-level DWARF file. It provides a mockable wrapper around a llvm::DWARFContext.
//
// This is currently a very leaky abstraction because a lot of code was written before it was
// created and that coded uses llvm objects directly. As a result, this has some accessors for
// the underlying LLVM objects. Mocks may return null for these. If a new test needs to be written
// for such code, wrappers should be added so that the code no longer needs the llvm objects and
// can use the mockable wrappers.
class DwarfBinary {
 public:
  virtual ~DwarfBinary() = default;

  // Getters for basic binary information.
  virtual std::string GetName() const = 0;
  virtual std::string GetBuildID() const = 0;
  virtual std::time_t GetModificationTime() const = 0;

  // Return whether this module has been given the opportunity to include symbols from the binary
  // itself, such as PLT entries.
  virtual bool HasBinary() const = 0;

  // Returns underlying LLVM objects. May be null in tests since the mock won't have this. See the
  // class comment above.
  virtual llvm::object::ObjectFile* GetLLVMObjectFile() = 0;
  virtual llvm::DWARFContext* GetLLVMContext() = 0;

  // Returns the extent of the mapped segments in memory.
  virtual uint64_t GetMappedLength() const = 0;

  // Returns symbols from the ELF File.
  virtual const std::map<std::string, llvm::ELF::Elf64_Sym>& GetELFSymbols() const = 0;
  virtual const std::map<std::string, uint64_t> GetPLTSymbols() const = 0;

  // Allows access to compile units in this binary by 0-based index.
  virtual size_t GetUnitCount() const = 0;
  virtual fxl::RefPtr<DwarfUnit> GetUnitAtIndex(size_t i) = 0;

  // Returns the DwarfUnit covering the given absolute address location. Can be null if there's
  // no unit that covers this area.
  fxl::RefPtr<DwarfUnit> UnitForAddress(const SymbolContext& symbol_context,
                                        TargetPointer absolute_address) {
    return UnitForRelativeAddress(symbol_context.AbsoluteToRelative(absolute_address));
  }

  // Like UnitForAddress but takes an address relative to the load address of the binary.
  virtual fxl::RefPtr<DwarfUnit> UnitForRelativeAddress(uint64_t relative_address) = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_BINARY_H_
