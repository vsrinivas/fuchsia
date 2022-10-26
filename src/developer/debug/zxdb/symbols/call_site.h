// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_H_

#include <optional>

#include "src/developer/debug/zxdb/symbols/arch.h"
#include "src/developer/debug/zxdb/symbols/symbol.h"

namespace zxdb {

class CallSite : public Symbol {
 public:
  // The return address relative to the module load address, if specified.
  std::optional<TargetPointer> return_pc() const { return return_pc_; }

  // The parameters associated with this call site. These symbols should be of type
  // CallSiteParameter.
  const std::vector<LazySymbol>& parameters() const { return parameters_; }

  // Additional information is also supported by DWARF which we have no current need for. These can
  // be added as required:
  //
  //   DW_AT_call_file / DW_AT_call_line / DW_AT_call_column
  //   DW_AT_call_origin
  //   DW_AT_call_tail_call
  //   DW_AT_call_target (Possibly useful, Clang currently sets this for virtual calls).
  //   DW_AT_call_target_clobbered
  //   DW_AT_type

 protected:
  const CallSite* AsCallSite() const override { return this; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(CallSite);
  FRIEND_MAKE_REF_COUNTED(CallSite);

  explicit CallSite(std::optional<TargetPointer> return_pc, std::vector<LazySymbol> params)
      : Symbol(DwarfTag::kCallSite), return_pc_(return_pc), parameters_(std::move(params)) {}

  std::optional<TargetPointer> return_pc_;

  std::vector<LazySymbol> parameters_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CALL_SITE_H_
