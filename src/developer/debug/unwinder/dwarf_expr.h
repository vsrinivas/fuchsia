// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_EXOR_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_EXOR_H_

#include <cstdint>
#include <vector>

#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

// This is a simple implementation of a dwarf expression evaluator, only intended to be used
// by the unwinder. A more sophisticated one can be found in zxdb::DwarfExprEval.
class DwarfExpr {
 public:
  DwarfExpr() = default;
  explicit DwarfExpr(Memory* expr, uint64_t begin, uint64_t length)
      : expr_(expr), expr_begin_(begin), expr_end_(begin + length) {}

  Error Eval(Memory* mem, const Registers& regs, uint64_t initial_value, uint64_t& result);

 private:
  Memory* expr_ = nullptr;
  uint64_t expr_begin_;
  uint64_t expr_end_;
};

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_DWARF_EXOR_H_
