// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_command_buffer.h"

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"

class TestExec : public TestCommandBuffer {};

// Tests submitting a simple batch that also provides a non-zero batch offset.
TEST_F(TestExec, SubmitBatchWithOffset) {
  constexpr uint32_t kBufferSize = 4096;
  constexpr uint32_t kMapPageCount = 1;
  constexpr uint32_t kDataSize = 4;
  // The user data will start at a non-zero offset.
  constexpr uint32_t kBatchOffset = 80;
  constexpr uint32_t gpu_addr = 0x10000;

  std::shared_ptr<MsdVslBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(kBufferSize, kMapPageCount, gpu_addr, &buffer));

  // Write a WAIT command at offset |kBatchOffset|.
  uint32_t* cmd_ptr;
  ASSERT_TRUE(buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, kBufferSize, kBatchOffset);
  MiWait::write(&buf_writer);
  ASSERT_TRUE(buffer->platform_buffer()->UnmapCpu());

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndPrepareBatch(buffer, kDataSize, kBatchOffset, semaphore->Clone(), &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  ASSERT_TRUE(device_->SubmitBatch(std::move(batch)).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

