// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/escher/renderer/frame.h"
#include "garnet/public/lib/escher/resources/resource.h"
#include "garnet/public/lib/escher/resources/resource_manager.h"
#include "garnet/public/lib/escher/test/gtest_escher.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace escher {

VK_TEST(Frame, CreateDestroyFrame) {
  auto escher = test::GetEscher()->GetWeakPtr();
  {
    auto frame = escher->NewFrame("test_frame", 0, false);
    frame->EndFrame(SemaphorePtr(), [] {});
  }
}

VK_TEST(Frame, ValidCommandBufferTypes) {
  auto escher = test::GetEscher()->GetWeakPtr();

  auto graphics_frame =
      escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kGraphics);
  EXPECT_EQ(CommandBuffer::Type::kGraphics, graphics_frame->cmds()->type());

  auto compute_frame =
      escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kCompute);
  EXPECT_EQ(CommandBuffer::Type::kCompute, compute_frame->cmds()->type());

  auto transfer_frame =
      escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kTransfer);
  EXPECT_EQ(CommandBuffer::Type::kTransfer, transfer_frame->cmds()->type());

  // End frames to clean up properly.
  graphics_frame->EndFrame(SemaphorePtr(), [] {});
  compute_frame->EndFrame(SemaphorePtr(), [] {});
  transfer_frame->EndFrame(SemaphorePtr(), [] {});
}

VK_TEST(Frame, InvalidCommandBufferType) {
  auto escher = test::GetEscher()->GetWeakPtr();

  EXPECT_DEATH_IF_SUPPORTED(
      escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kEnumCount),
      "");
}

}  // namespace escher
