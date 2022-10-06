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
        nullptr, [&called](OutputBuffer output, std::vector<Err> errors) {
          called = true;
          EXPECT_TRUE(output.AsString().empty());
          EXPECT_TRUE(errors.empty());
        });
  }
  EXPECT_TRUE(called);
}

TEST(CommandContext, AsyncOutputAndErrors) {
  // This test constructs two AsyncOutputBuffers, one depending on the other. We only keep a
  // reference to the inner one. The ConsoleContext should keep the reference to the outer one
  // to keep it alive as long as it's not complete.
  auto inner_async_output = fxl::MakeRefCounted<AsyncOutputBuffer>();

  bool called = false;
  {
    auto my_context = fxl::MakeRefCounted<OfflineCommandContext>(
        nullptr, [&called](OutputBuffer output, std::vector<Err> errors) {
          called = true;

          EXPECT_EQ("Some output\nAsync output\n", output.AsString());

          EXPECT_EQ(1u, errors.size());
          EXPECT_EQ("Some error", errors[0].msg());
        });

    my_context->ReportError(Err("Some error"));
    my_context->Output("Some output\n");

    auto outer_async_output = fxl::MakeRefCounted<AsyncOutputBuffer>();
    outer_async_output->Append(inner_async_output);
    my_context->Output(outer_async_output);
    outer_async_output->Complete();
  }
  // Even though our reference went out-of-scope, the async output is still active.
  EXPECT_FALSE(called);

  inner_async_output->Append("Async output\n");
  inner_async_output->Complete();

  // Marking the async output complete should have marked the context done.
  EXPECT_TRUE(called);
}

}  // namespace zxdb
