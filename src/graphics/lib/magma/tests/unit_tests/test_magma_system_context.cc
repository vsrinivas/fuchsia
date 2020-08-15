// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "helper/command_buffer_helper.h"
#include "mock/mock_msd.h"

TEST(MagmaSystemContext, ExecuteCommandBuffer_Normal) {
  auto cmd_buf = CommandBufferHelper::Create();
  EXPECT_TRUE(cmd_buf->Execute());

  auto num_resources = cmd_buf->abi_cmd_buf()->resource_count;
  auto system_resources = cmd_buf->resources();
  auto submitted_msd_resources =
      MsdMockContext::cast(cmd_buf->ctx())->last_submitted_exec_resources();

  EXPECT_EQ(system_resources.size(), num_resources);
  EXPECT_EQ(submitted_msd_resources.size(), num_resources);

  for (uint32_t i = 0; i < num_resources; i++) {
    EXPECT_EQ(system_resources[i]->msd_buf(), submitted_msd_resources[i]);
  }
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidBatchBufferIndex) {
  auto cmd_buf = CommandBufferHelper::Create();
  cmd_buf->abi_cmd_buf()->batch_buffer_resource_index =
      CommandBufferHelper::kNumResources;  // smallest invalid value
  EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidBatchStartOffset) {
  auto cmd_buf = CommandBufferHelper::Create();
  cmd_buf->abi_cmd_buf()->batch_start_offset = UINT32_MAX;
  EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidExecResourceHandle) {
  auto cmd_buf = CommandBufferHelper::Create();
  cmd_buf->abi_resources()[0].buffer_id = 0xdeadbeefdeadbeef;
  EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_DuplicateExecResourceHandle) {
  auto cmd_buf = CommandBufferHelper::Create();
  cmd_buf->abi_resources()[1].buffer_id = cmd_buf->abi_resources()[0].buffer_id;
  EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidWaitSemaphore) {
  auto cmd_buf = CommandBufferHelper::Create();
  for (uint32_t i = 0; i < CommandBufferHelper::kWaitSemaphoreCount; i++) {
    cmd_buf->abi_wait_semaphore_ids()[i] = 0;
  }
  EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidSignalSemaphore) {
  auto cmd_buf = CommandBufferHelper::Create();
  for (uint32_t i = 0; i < CommandBufferHelper::kSignalSemaphoreCount; i++) {
    cmd_buf->abi_signal_semaphore_ids()[i] = 0;
  }
  EXPECT_FALSE(cmd_buf->Execute());
}
