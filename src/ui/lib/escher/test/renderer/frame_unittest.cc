// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/frame.h"

#include <gtest/gtest.h>

#include "src/ui/lib/escher/resources/resource.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"

namespace escher {

using FrameTest = test::TestWithVkValidationLayer;

VK_TEST_F(FrameTest, CreateDestroyFrame) {
  auto escher = test::GetEscher()->GetWeakPtr();
  {
    auto frame = escher->NewFrame("test_frame", 0, false);
    frame->EndFrame(SemaphorePtr(), [] {});
  }
}

VK_TEST_F(FrameTest, ValidCommandBufferTypes) {
  auto escher = test::GetEscher()->GetWeakPtr();

  auto graphics_frame = escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kGraphics);
  EXPECT_EQ(CommandBuffer::Type::kGraphics, graphics_frame->cmds()->type());

  auto compute_frame = escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kCompute);
  EXPECT_EQ(CommandBuffer::Type::kCompute, compute_frame->cmds()->type());

  auto transfer_frame = escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kTransfer);
  EXPECT_EQ(CommandBuffer::Type::kTransfer, transfer_frame->cmds()->type());

  // End frames to clean up properly.
  graphics_frame->EndFrame(SemaphorePtr(), [] {});
  compute_frame->EndFrame(SemaphorePtr(), [] {});
  transfer_frame->EndFrame(SemaphorePtr(), [] {});
}

VK_TEST_F(FrameTest, InvalidCommandBufferType) {
  auto escher = test::GetEscher()->GetWeakPtr();

  EXPECT_DEATH_IF_SUPPORTED(
      escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kEnumCount), "");
}

VK_TEST_F(FrameTest, SubmitPartialFrameCreatesCleanCommandBuffer) {
  auto escher = test::GetEscher()->GetWeakPtr();
  auto frame = escher->NewFrame("test_frame", 0, false, CommandBuffer::Type::kTransfer);
  EXPECT_EQ(CommandBuffer::Type::kTransfer, frame->cmds()->type());
  uint64_t command_buffer_sequence_number = frame->command_buffer_sequence_number();

  frame->SubmitPartialFrame(SemaphorePtr());

  EXPECT_EQ(CommandBuffer::Type::kTransfer, frame->cmds()->type());
  EXPECT_NE(command_buffer_sequence_number, frame->command_buffer_sequence_number());
  EXPECT_EQ(frame->command_buffer_sequence_number(), frame->cmds()->sequence_number());
  frame->EndFrame(SemaphorePtr(), [] {});
}

}  // namespace escher
