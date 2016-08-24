// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper/command_buffer_helper.h"
#include "mock/mock_msd.h"
#include "gtest/gtest.h"

TEST(MagmaSystemContext, ExecuteCommandBuffer_Normal)
{
    auto cmd_buf = CommandBufferHelper::Create();
    EXPECT_TRUE(cmd_buf->Execute());

    auto num_resources = cmd_buf->abi_cmd_buf()->num_resources;
    auto system_resources = cmd_buf->resources();
    auto submitted_msd_resources =
        MsdMockContext::cast(cmd_buf->ctx())->last_submitted_exec_resources();

    EXPECT_EQ(system_resources.size(), num_resources);
    EXPECT_EQ(submitted_msd_resources.size(), num_resources);

    for (uint32_t i = 0; i < num_resources; i++) {
        EXPECT_EQ(system_resources[i]->msd_buf(), submitted_msd_resources[i]);
    }
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidBatchBufferIndex)
{
    auto cmd_buf = CommandBufferHelper::Create();
    cmd_buf->abi_cmd_buf()->batch_buffer_resource_index =
        CommandBufferHelper::kNumResources; // smallest invalid value
    EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidExecResourceHandle)
{
    auto cmd_buf = CommandBufferHelper::Create();
    cmd_buf->abi_cmd_buf()->resources[0].buffer_handle = 0xdeadbeef;
    EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_DuplicateExecResourceHandle)
{
    auto cmd_buf = CommandBufferHelper::Create();
    cmd_buf->abi_cmd_buf()->resources[1].buffer_handle =
        cmd_buf->abi_cmd_buf()->resources[0].buffer_handle;
    EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidRelocationOffset)
{
    auto cmd_buf = CommandBufferHelper::Create();
    for (uint32_t i = 0; i < cmd_buf->abi_cmd_buf()->resources[0].num_relocations; i++) {
        cmd_buf->abi_cmd_buf()->resources[0].relocations[i].offset =
            CommandBufferHelper::kBufferSize - sizeof(uint32_t) + 1; // smallest invalid offset
    }
    EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidRelocationTargetBufferIndex)
{
    auto cmd_buf = CommandBufferHelper::Create();
    cmd_buf->abi_cmd_buf()->resources[0].relocations[0].target_resource_index =
        CommandBufferHelper::kNumResources; // smallest invalid value
    EXPECT_FALSE(cmd_buf->Execute());
}

TEST(MagmaSystemContext, ExecuteCommandBuffer_InvalidRelocationTargetOffset)
{
    auto cmd_buf = CommandBufferHelper::Create();
    for (uint32_t i = 0; i < cmd_buf->abi_cmd_buf()->resources[0].num_relocations; i++) {
        cmd_buf->abi_cmd_buf()->resources[0].relocations[i].target_offset =
            CommandBufferHelper::kBufferSize - sizeof(uint32_t) + 1; // smallest invalid offset
    }
    EXPECT_FALSE(cmd_buf->Execute());
}
