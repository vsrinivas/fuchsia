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
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc));
}

// Tests submitting buffers from different contexts from the same connection.
TEST_F(TestExec, SubmitBatchesMultipleContexts) {
  // Create an additional context on the default connection.
  auto context2 = MsdVslContext::Create(default_connection(), default_address_space(),
                                        device_->GetRingbuffer());
  ASSERT_NE(context2, nullptr);

  device_->StartDeviceThread();

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4,
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc));
  ASSERT_EQ(device_->configured_address_space_.get(), default_address_space().get());

  BufferDesc buffer_desc2 = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4,
      .batch_offset = 0,
      .gpu_addr = 0x20000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context2, buffer_desc2));
  ASSERT_EQ(device_->configured_address_space_.get(), default_address_space().get());
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
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc, &submitted_buffer));

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
  ASSERT_TRUE(default_connection()->ReleaseMapping(submitted_buffer->platform_buffer(),
                                                   buffer_desc.gpu_addr));

  // Map the second buffer at the same GPU address and try submitting it.
  magma::Status status = default_connection()->MapBufferGpu(
      msd_buffer, buffer_desc.gpu_addr, 0 /* page_offset */, buffer_desc.map_page_count);
  ASSERT_TRUE(status.ok());

  // Submit the batch and verify we get a completion event.
  auto semaphore = magma::PlatformSemaphore::Create();
  EXPECT_NE(semaphore, nullptr);

  std::unique_ptr<CommandBuffer> batch;
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(default_context(), msd_buffer,
                                                buffer_desc.data_size, buffer_desc.batch_offset,
                                                semaphore->Clone(), &batch));
  ASSERT_TRUE(batch->IsValidBatchBuffer());

  // The context should determine that TLB flushing is required.
  ASSERT_TRUE(default_context()->SubmitBatch(std::move(batch)).ok());

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

    auto batch =
        std::make_unique<EventBatch>(default_context(), wait_semaphores, signal_semaphores);
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
    CreateAndMapBuffer(default_context(), buffer_size, buffer_size / magma::page_size(),
                       next_gpu_addr, &buffer);
    ASSERT_NE(buffer, nullptr);
    next_gpu_addr += buffer_size;

    // Write a basic command into the buffer.
    WriteWaitCommand(buffer, 0 /* offset */);

    std::unique_ptr<CommandBuffer> batch;
    CreateAndPrepareBatch(default_context(), buffer, data_size, 0, semaphores[i]->Clone(), &batch);
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

TEST_F(TestExec, SwitchAddressSpace) {
  device_->StartDeviceThread();

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  // Create, map and submit another buffer.
  // This will wait for execution to complete.
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc));

  // Drop the client before creating a new one.
  DropDefaultClient();

  std::unique_ptr<Client> client;
  constexpr uint32_t kNewClientAddressSpaceIndex = 10;
  // Replace the existing address space, connection and context.
  ASSERT_NO_FATAL_FAILURE(CreateClient(kNewClientAddressSpaceIndex, &client));
  BufferDesc buffer_desc2 = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x20000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(client->context, buffer_desc2));
}

// Tests submitting buffers from many clients, each with different address spaces.
TEST_F(TestExec, SwitchMultipleAddressSpaces) {
  device_->StartDeviceThread();

  constexpr uint32_t kNumClients = 10;

  std::vector<std::unique_ptr<Client>> clients;
  for (unsigned int i = 0; i < kNumClients; i++) {
    std::unique_ptr<Client> client;
    ASSERT_NO_FATAL_FAILURE(CreateClient(i + 10 /* address_space_index */, &client));
    clients.emplace_back(std::move(client));
  }

  constexpr uint32_t kBaseGpuAddr = 0x10000;
  for (unsigned int i = 0; i < 2; i++) {
    for (unsigned int j = 0; j < kNumClients; j++) {
      BufferDesc buffer_desc = {
          .buffer_size = 4096,
          .map_page_count = 1,
          .data_size = 8,
          .batch_offset = 0,
          // Use different gpu addresses to make sure the GPU is not just using the first mapping.
          .gpu_addr = static_cast<uint32_t>(kBaseGpuAddr + (magma::page_size() * (i + j))),
      };
      ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(clients[j]->context, buffer_desc));
      ASSERT_EQ(device_->configured_address_space_.get(), clients[j]->address_space.get());
    }
  }
}
