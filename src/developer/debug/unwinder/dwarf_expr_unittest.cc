// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/dwarf_expr.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/registers.h"

namespace unwinder {

namespace {

Error EvaluateWithError(const std::vector<uint8_t>& expr,
                        const std::map<RegisterID, uint64_t>& register_values,
                        uint64_t initial_value, uint64_t& result) {
  LocalMemory mem;
  Registers regs(Registers::Arch::kArm64);
  for (auto [reg, val] : register_values) {
    Error err = regs.Set(reg, val);
    EXPECT_TRUE(err.ok()) << err.msg();
  }
  DwarfExpr dwarf_expr(&mem, reinterpret_cast<uint64_t>(expr.data()), expr.size());
  return dwarf_expr.Eval(&mem, regs, initial_value, result);
}

uint64_t Evaluate(const std::vector<uint8_t>& expr,
                  const std::map<RegisterID, uint64_t>& register_values = {},
                  uint64_t initial_value = 0) {
  uint64_t res;
  Error err = EvaluateWithError(expr, register_values, initial_value, res);
  EXPECT_TRUE(err.ok()) << err.msg();
  return res;
}

TEST(DwarfExpr, Const) {
  EXPECT_EQ(10u, Evaluate({}, {}, 10));
  EXPECT_EQ(20u, Evaluate({DW_OP_lit20}));
  EXPECT_EQ(30u, Evaluate({DW_OP_const2s, 30, 0}));
  EXPECT_EQ(40u, Evaluate({DW_OP_constu, 40}));
}

TEST(DwarfExpr, Comparison) {
  EXPECT_EQ(0u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_eq}));
  EXPECT_EQ(1u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_le}));
  EXPECT_EQ(0u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_gt}));
}

TEST(DwarfExpr, Arithmetic) {
  // 1 + 1
  EXPECT_EQ(2u, Evaluate({DW_OP_lit1, DW_OP_plus}, {}, 1));

  // 10 - (3 * 3) / 4
  EXPECT_EQ(8u, Evaluate({DW_OP_lit10, DW_OP_lit3, DW_OP_lit3, DW_OP_mul, DW_OP_lit4, DW_OP_div,
                          DW_OP_minus}));

  // 0 + 30 + 70
  EXPECT_EQ(100u, Evaluate({DW_OP_lit30, DW_OP_plus_uconst, 70, DW_OP_plus}));

  // Invalid
  uint64_t res;
  EXPECT_TRUE(EvaluateWithError({DW_OP_eq}, {}, 0, res).has_err());
}

TEST(DwarfExpr, StackOperations) {
  // Dup.
  EXPECT_EQ(3u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_dup}));

  // Swap and minus.
  EXPECT_EQ(1u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_swap, DW_OP_minus}));

  // Pick.
  EXPECT_EQ(1u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_pick, 2}));

  // Over.
  EXPECT_EQ(2u, Evaluate({DW_OP_lit1, DW_OP_lit2, DW_OP_lit3, DW_OP_over}));

  // Stack too shallow.
  uint64_t res;
  EXPECT_TRUE(EvaluateWithError({DW_OP_drop}, {}, 0, res).has_err());
}

TEST(DwarfExpr, Breg) {
  // breg18 - 8
  EXPECT_EQ(0x1000u, Evaluate({DW_OP_breg18, (-8) & 0x7f}, {{RegisterID::kArm64_x18, 0x1008}}));

  // *(breg0 + 0) & *(breg16 + 8)
  uint64_t data[] = {0xBEEF0812, 0xBEEF0724};
  EXPECT_EQ(0xBEEF0000u,
            Evaluate({DW_OP_breg0, 0, DW_OP_deref, DW_OP_breg16, 8, DW_OP_deref, DW_OP_and},
                     {
                         {RegisterID::kArm64_x0, reinterpret_cast<uint64_t>(data)},
                         {RegisterID::kArm64_x16, reinterpret_cast<uint64_t>(data)},
                     }));

  // breg3 not present.
  uint64_t res;
  EXPECT_TRUE(EvaluateWithError({DW_OP_breg3, 0}, {}, 0, res).has_err());
}

TEST(DwarfExpr, ControlFlow) {
  // breg0 + 1 <= 1 ? breg1 + 0 : breg2 + 0
  std::vector<uint8_t> expr = {DW_OP_breg0, 1, DW_OP_lit1, DW_OP_le, DW_OP_bra, 5,           0,
                               DW_OP_breg2, 0, DW_OP_skip, 2,        0,         DW_OP_breg1, 0};
  EXPECT_EQ(10u, Evaluate(expr, {
                                    {RegisterID::kArm64_x0, 0},
                                    {RegisterID::kArm64_x1, 10},
                                    {RegisterID::kArm64_x2, 20},
                                }));
  EXPECT_EQ(20u, Evaluate(expr, {
                                    {RegisterID::kArm64_x0, 1},
                                    {RegisterID::kArm64_x1, 10},
                                    {RegisterID::kArm64_x2, 20},
                                }));

  // Condition with wrong offset.
  uint64_t res;
  EXPECT_TRUE(EvaluateWithError({DW_OP_bra, 3, 0, DW_OP_lit10, DW_OP_lit20}, {}, 1, res).has_err());
}

}  // namespace

}  // namespace unwinder
