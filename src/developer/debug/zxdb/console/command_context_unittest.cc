// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_context.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(CommandContext, Empty) {
  bool called = false;
  {
    auto my_context = fxl::MakeRefCounted<OfflineCommandContext>(
        nullptr, [&called](std::vector<OutputBuffer> outputs, std::vector<Err> errors) {
          called = true;
          EXPECT_TRUE(outputs.empty());
          EXPECT_TRUE(errors.empty());
        });
  }
  EXPECT_TRUE(called);
}

TEST(CommandContext, AsyncOutputAndErrors) {
  auto async_output = fxl::MakeRefCounted<AsyncOutputBuffer>();
  bool called = false;
  {
    auto my_context = fxl::MakeRefCounted<OfflineCommandContext>(
        nullptr, [&called](std::vector<OutputBuffer> outputs, std::vector<Err> errors) {
          called = true;

          EXPECT_EQ(2u, outputs.size());
          EXPECT_EQ("Some output", outputs[0].AsString());
          EXPECT_EQ("Async output", outputs[1].AsString());

          EXPECT_EQ(1u, errors.size());
          EXPECT_EQ("Some error", errors[0].msg());
        });

    my_context->ReportError(Err("Some error"));
    my_context->Output("Some output");
    my_context->Output(async_output);
  }
  // Even though our reference went out-of-scope, the async output is still active.
  EXPECT_FALSE(called);

  async_output->Append("Async output");
  async_output->Complete();

  // Marking the async output complete should have marked the context done.
  EXPECT_TRUE(called);
}

}  // namespace zxdb
