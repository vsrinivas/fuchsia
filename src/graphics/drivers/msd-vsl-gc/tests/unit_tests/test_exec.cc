// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_command_buffer.h"

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"

class TestExec : public TestCommandBuffer {};

// Tests submitting a simple batch that also provides a non-zero batch offset.
TEST_F(TestExec, SubmitBatchWithOffset) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 4,
    // The user data will start at a non-zero offset.
    .batch_offset = 80,
    .gpu_addr = 0x10000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(buffer_desc));
}

// Tests reusing a gpu address after unmapping it.
//
// Creates two buffers, submits one and releases its GPU mapping.
// Maps the second buffer at the same GPU address and verifies that the
// GPU accesses the correct buffer.
TEST_F(TestExec, ReuseGpuAddress) {
  BufferDesc buffer_desc = {
    .buffer_size = 4096,
    .map_page_count = 1,
    .data_size = 8,
    .batch_offset = 0,
    .gpu_addr = 0x10000,
  };
  constexpr uint32_t unmapped_gpu_addr = 0x50000;

  // Create a buffer without mapping it.
  std::shared_ptr<MsdVslBuffer> msd_buffer;
  ASSERT_NO_FATAL_FAILURE(CreateMsdBuffer(buffer_desc.buffer_size, &msd_buffer));

  // Create, map and submit another buffer.
  // This will wait for execution to complete.
  std::shared_ptr<MsdVslBuffer> submitted_buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(buffer_desc, &submitted_buffer));

  // Write a bad instruction into the mapped buffer.
  // If the GPU attempts to run this instruction, it will cause a MMU exception and
  // the next |SubmitBatch| will fail.
  uint32_t* cmd_ptr;
  ASSERT_TRUE(submitted_buffer->platform_buffer()->MapCpu(reinterpret_cast<void**>(&cmd_ptr)));
  BufferWriter buf_writer(cmd_ptr, buffer_desc.buffer_size, 0);
  // Link to somewhere unmapped.
  MiLink::write(&buf_writer, 1, unmapped_gpu_addr);
  ASSERT_TRUE(submitted_buffer->platform_buffer()->UnmapCpu());

  // Free the GPU address.
  std::vector<std::shared_ptr<GpuMapping>> mappings;
  address_space()->ReleaseBuffer(submitted_buffer->platform_buffer(), &mappings);

  // Map the second buffer at the same GPU address and try submitting it.
  std::shared_ptr<GpuMapping> gpu_mapping;
  magma::Status status = AddressSpace::MapBufferGpu(
      address_space(), msd_buffer, buffer_desc.gpu_addr, 0 /* page_offset */,
      buffer_desc.map_page_count, &gpu_mapping);
  ASSERT_TRUE(status.ok());
  ASSERT_NE(gpu_mapping, nullptr);

  ASSERT_TRUE(address_space()->AddMapping(std::move(gpu_mapping)));

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(
      msd_buffer, buffer_desc.data_size, buffer_desc.batch_offset, semaphore->Clone(), &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  ASSERT_TRUE(device_->SubmitBatch(std::move(batch), true /* do_flush */).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

