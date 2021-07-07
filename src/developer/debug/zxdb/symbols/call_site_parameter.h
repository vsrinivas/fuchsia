// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_PARAMETER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_PARAMETER_H_

#include "src/developer/debug/zxdb/symbols/dwarf_expr.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

// Represents a DW_TAG_call_site_parameter.
class CallSiteParameter : public Symbol {
 public:
  // The DWARF register number that corresponds to this location.
  //
  // This corresponds to the decoded register number of the DW_AT_location of the call site
  // parameter. Theoretically, the DW_AT_location could specify any location in any way, but the
  // current compilers we support always output a single-byte operation of DW_OP_reg? to indicate
  // the register number.
  //
  // More complex locations are not useful for call site parameters since the whole point is to
  // specify the registers upon function call. They could be expressed as "DW_OP_regx, <regnum>"
  // but currently compilers don't do that and the expression is longer anyway.
  //
  // If we see more complex expressions, we should probably add a real VariableLocation here for
  // uniform evaluation rather than pushing more decode logic into the DwarfSymbolFactory. Perhaps
  // this class could have a helper to decode it.
  std::optional<uint32_t> location_register_num() const { return location_register_num_; }

  // The expression indicating the value of the location. This could be empty() if it's not
  // specified in the symbols.
  const DwarfExpr& value_expr() const { return value_expr_; }

  // Additional information is also supported by DWARF which we have no current need for. These can
  // be added as required:
  //
  //   DW_AT_call_data_location
  //   DW_AT_call_data_value
  //   DW_AT_call_parameter
  //   DW_AT_name
  //   DW_AT_type

 protected:
  const CallSiteParameter* AsCallSiteParameter() const override { return this; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CallSiteParameter);
  FRIEND_MAKE_REF_COUNTED(CallSiteParameter);

  CallSiteParameter(std::optional<uint32_t> register_num, DwarfExpr value_expr)
      : location_register_num_(register_num), value_expr_(std::move(value_expr)) {}

  std::optional<uint32_t> location_register_num_;
  DwarfExpr value_expr_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_PARAMETER_H_
