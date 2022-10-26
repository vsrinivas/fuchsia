// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_IMPL_H_

#include "src/developer/debug/zxdb/symbols/dwarf_unit.h"
#include "src/developer/debug/zxdb/symbols/line_table_impl.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace llvm {
class DWARFDie;
class DWARFUnit;
}  // namespace llvm

namespace zxdb {

class DwarfBinaryImpl;

class DwarfUnitImpl : public DwarfUnit {
 public:
  // Construct with fxl::MakeRefCounted<DwarfUnitImpl>().

  // Possibly null (this class may outlive the DwarfBinary).
  llvm::DWARFUnit* unit() const { return binary_ ? unit_ : nullptr; }

  // DwarfUnit implementation.
  uint64_t GetOffset() const override;
  uint64_t FunctionDieOffsetForRelativeAddress(uint64_t relative_address) const override;
  std::string GetCompilationDir() const override;
  const LineTable& GetLineTable() const override;
  const llvm::DWARFDebugLine::LineTable* GetLLVMLineTable() const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(DwarfUnitImpl);
  FRIEND_MAKE_REF_COUNTED(DwarfUnitImpl);

  DwarfUnitImpl(DwarfBinaryImpl* binary, llvm::DWARFUnit* unit);

  // The binary that owns us.
  fxl::WeakPtr<DwarfBinaryImpl> binary_;

  // This pointer is owned by LLVM's DWARFContext object. Integrating LLVM's memory model with ours
  // here is a bit messy. In practice this means that the DwarfBinary outlives all DwarfUnits, and
  // users should check that the binary_ is still valid before dereferencing.
  llvm::DWARFUnit* unit_;

  // The line table. Computed lazily.
  mutable std::optional<LineTableImpl> line_table_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_UNIT_IMPL_H_
