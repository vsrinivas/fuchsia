// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_stream.h"

#include <gtest/gtest.h>

namespace zxdb {

// This also tests the VmOp operator<<.
TEST(VmOp, VmStreamToString) {
  VmStream stream;
  EXPECT_EQ("", VmStreamToString(stream));

  stream.push_back(VmOp::MakeLiteral(ExprValue(92)));
  stream.push_back(VmOp::MakeDup());
  stream.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kPlus, "+", 0)));
  stream.push_back(VmOp::MakeJumpIfFalse(1));
  stream.push_back(VmOp::MakeCallback0(
      [](const fxl::RefPtr<EvalContext>&) -> ErrOrValue { return Err("Error"); }));

  EXPECT_EQ(
      "0: Literal(int32_t(92))\n"
      "1: Dup()\n"
      "2: Binary(+)\n"
      "3: JumpIfFalse(1)\n"
      "4: Callback0()\n",
      VmStreamToString(stream));
}

}  // namespace zxdb
