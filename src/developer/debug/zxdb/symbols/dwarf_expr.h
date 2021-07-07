// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_EXPR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_EXPR_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "src/developer/debug/zxdb/symbols/lazy_symbol.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// Represents a DWARF expression. This is a list of bytes that encodes a simple stack machine.
// This expression can also reference other parts of the symbols so the symbol associated with
// it is also stored.
//
// These expressions are evaluated by the DwarfExprEval.
class DwarfExpr {
 public:
  DwarfExpr() = default;

  // The source can be an empty UncachedLazySymbol if there is no corresponding source symbol for
  // this expression. This should only be the case for tests. This will mean calls to GetAddrBase()
  // will fail.
  //
  // The symbol needs to be uncached because this is normally used as a back-reference. A variable
  // would have one or more expressions indicating its location, and the expression would refer back
  // to the variable. Using an uncached symbol prevents reference cycles.
  explicit DwarfExpr(std::vector<uint8_t> data, UncachedLazySymbol source = UncachedLazySymbol())
      : data_(std::move(data)), source_(std::move(source)) {}

  bool empty() const { return data_.empty(); }

  const std::vector<uint8_t>& data() const { return data_; }
  const UncachedLazySymbol& source() const { return source_; }

  // Returns the DW_AT_addr_base attribute associated with this expression. It will be on the
  // compilation unit associated with the source of the expression.
  //
  // This attribute points to the beginning of the compilation unit's contribution to the
  // .debug_addr section of the module.
  //
  // Returns a nullopt if there is none (either the source isn't known or the unit has no addr base
  // attribute).
  std::optional<uint64_t> GetAddrBase() const;

 private:
  std::vector<uint8_t> data_;
  UncachedLazySymbol source_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_DWARF_EXPR_H_
