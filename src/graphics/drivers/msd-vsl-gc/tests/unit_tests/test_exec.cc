// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/graphics/drivers/msd-vsl-gc/src/command_buffer.h"
#include "test_command_buffer.h"

class TestExec : public TestCommandBuffer {};

// Tests submitting a simple batch that also provides a non-zero batch offset.
TEST_F(TestExec, SubmitBatchWithOffset) {
  device_->StartDeviceThread();

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
  device_->StartDeviceThread();

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
  ASSERT_TRUE(
      connection()->ReleaseMapping(submitted_buffer->platform_buffer(), buffer_desc.gpu_addr));

  // Map the second buffer at the same GPU address and try submitting it.
  magma::Status status = connection()->MapBufferGpu(
      msd_buffer, buffer_desc.gpu_addr, 0 /* page_offset */, buffer_desc.map_page_count);
  ASSERT_TRUE(status.ok());

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(
      msd_buffer, buffer_desc.data_size, buffer_desc.batch_offset, semaphore->Clone(), &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  // The context should determine that TLB flushing is required.
  ASSERT_TRUE(context()->SubmitBatch(std::move(batch)).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

TEST_F(TestExec, Backlog) {
  uint32_t num_batches = MsdVslDevice::kNumEvents * 3;
  auto semaphores = std::make_unique<std::unique_ptr<magma::PlatformSemaphore>[]>(num_batches);

  for (unsigned int i = 0; i < num_batches; i++) {
    semaphores[i] = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphores[i], nullptr);

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
    std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
    signal_semaphores.emplace_back(semaphores[i]->Clone());

    auto batch = std::make_unique<EventBatch>(context(), wait_semaphores, signal_semaphores);
    ASSERT_EQ(MAGMA_STATUS_OK, device_->SubmitBatch(std::move(batch), false /* do_flush */).get());
  }

  // This will start processing all queued batches. Some of the batches will be added
  // to the backlog and will be processed once earlier batches complete.
  device_->StartDeviceThread();
  device_->device_request_semaphore_->Signal();

  // Wait for all the batches to complete.
  for (unsigned int i = 0; i < num_batches; i++) {
    constexpr uint64_t kTimeoutMs = 1000;
    ASSERT_EQ(MAGMA_STATUS_OK, semaphores[i]->Wait(kTimeoutMs).get());
  }
}

// Tests that the driver and client do not get stuck when an invalid batch is submitted.
TEST_F(TestExec, BacklogWithInvalidBatch) {
  // Try to submit 2 more events than available.
  uint32_t num_batches = MsdVslDevice::kNumEvents + 2;
  auto semaphores = std::make_unique<std::unique_ptr<magma::PlatformSemaphore>[]>(num_batches);

  constexpr uint32_t default_data_size = 0x4;
  // Make the second last batch submit a larger data size than supported.
  // |SubmitCommandBuffer| will fail on this batch.
  uint32_t invalid_batch_idx = num_batches - 2;
  constexpr uint32_t invalid_data_size = 0xF0000;

  uint32_t next_gpu_addr = 0x10000;

  for (unsigned int i = 0; i < num_batches; i++) {
    semaphores[i] = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphores[i], nullptr);

    uint32_t data_size = i == invalid_batch_idx ? invalid_data_size : default_data_size;
    uint32_t buffer_size = magma::round_up(data_size + 8, magma::page_size());

    std::shared_ptr<MsdVslBuffer> buffer;
    CreateAndMapBuffer(buffer_size, buffer_size / magma::page_size(), next_gpu_addr, &buffer);
    ASSERT_NE(buffer, nullptr);
    next_gpu_addr += buffer_size;

    // Write a basic command into the buffer.
    WriteWaitCommand(buffer, 0 /* offset */);

    std::unique_ptr<CommandBuffer> batch;
    CreateAndPrepareBatch(buffer, data_size, 0, semaphores[i]->Clone(), &batch);
    ASSERT_NE(batch, nullptr);
    ASSERT_EQ(MAGMA_STATUS_OK, device_->SubmitBatch(std::move(batch), false /* do_flush */).get());
  }
  device_->StartDeviceThread();
  device_->device_request_semaphore_->Signal();

  // The driver should drop any invalid batches, so we expect all semaphores to complete.
  for (unsigned int i = 0; i < num_batches; i++) {
    constexpr uint64_t kTimeoutMs = 1000;
    ASSERT_EQ(MAGMA_STATUS_OK, semaphores[i]->Wait(kTimeoutMs).get());
  }

  for (unsigned int i = 0; i < MsdVslDevice::kNumEvents; i++) {
    ASSERT_TRUE(!device_->events_[i].allocated);
  }
}
