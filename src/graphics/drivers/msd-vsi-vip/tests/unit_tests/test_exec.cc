// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
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

// Verifies reset while GPU is busy.
TEST_F(TestExec, ResetAfterSubmit) {
  for (uint32_t i = 0; i < 100; i++) {
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

    EXPECT_TRUE(device_->HardwareReset());

    Release();
    ASSERT_NO_FATAL_FAILURE(SetUp());
  }
}

// Tests submitting buffers from different contexts from the same connection.
TEST_F(TestExec, SubmitBatchesMultipleContexts) {
  // Create an additional context on the default connection.
  auto context2 = MsdVsiContext::Create(default_connection(), default_address_space(),
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
  std::shared_ptr<MsdVsiBuffer> msd_buffer;
  ASSERT_NO_FATAL_FAILURE(CreateMsdBuffer(buffer_desc.buffer_size, &msd_buffer));

  // Create, map and submit another buffer.
  // This will wait for execution to complete.
  std::shared_ptr<MsdVsiBuffer> submitted_buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc,
                                                std::nullopt /* csb */, &submitted_buffer));

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
  ASSERT_NO_FATAL_FAILURE(CreateAndPrepareBatch(
      default_context(), msd_buffer, buffer_desc.data_size, buffer_desc.batch_offset,
      semaphore->Clone(), std::nullopt /* csb */, &batch));
  ASSERT_TRUE(batch->IsValidBatch());

  // The context should determine that TLB flushing is required.
  ASSERT_TRUE(default_context()->SubmitBatch(std::move(batch)).ok());

  constexpr uint64_t kTimeoutMs = 1000;
  EXPECT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());
}

TEST_F(TestExec, Backlog) {
  uint32_t num_batches = MsdVsiDevice::kNumEvents * 3;
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
  uint32_t num_batches = MsdVsiDevice::kNumEvents + 2;
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

    std::shared_ptr<MsdVsiBuffer> buffer;
    CreateAndMapBuffer(default_context(), buffer_size, buffer_size / magma::page_size(),
                       next_gpu_addr, &buffer);
    ASSERT_NE(buffer, nullptr);
    next_gpu_addr += buffer_size;

    // Write a basic command into the buffer.
    WriteWaitCommand(buffer, 0 /* offset */);

    std::unique_ptr<CommandBuffer> batch;
    CreateAndPrepareBatch(default_context(), buffer, data_size, 0, semaphores[i]->Clone(),
                          std::nullopt /* csb */, &batch);
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

  for (unsigned int i = 0; i < MsdVsiDevice::kNumEvents; i++) {
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

TEST_F(TestExec, SubmitContextStateBufferSameContext) {
  // Allocate the context state buffers before starting the device thread,
  // as this needs to allocate interrupt events.
  std::unique_ptr<FakeContextStateBuffer> csb1;
  std::unique_ptr<FakeContextStateBuffer> csb2;
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, default_context(), 0x10000, &csb1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, default_context(), 0x20000, &csb2));

  device_->StartDeviceThread();

  // Submit 2 batches with context state buffers in the same address space.
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x30000,
  };
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(default_context(), buffer_desc, csb1->ExecResource()));

  buffer_desc.gpu_addr = 0x40000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(default_context(), buffer_desc, csb2->ExecResource()));

  // Only the first context state buffer should be executed.
  ASSERT_NO_FATAL_FAILURE(csb1->WaitForCompletion());
  ASSERT_EQ(device_->num_events_completed_, 3u);  // 1 context state buffer and 2 command buffers
}

TEST_F(TestExec, SubmitEventBeforeContextStateBuffer) {
  // Allocate the context state buffer before starting the device thread,
  // as this needs to allocate an interrupt event.
  std::unique_ptr<FakeContextStateBuffer> csb1;
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, default_context(), 0x10000, &csb1));

  device_->StartDeviceThread();

  // Submit an event batch.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  signal_semaphores.emplace_back(semaphore->Clone());

  auto batch = std::make_unique<EventBatch>(default_context(), wait_semaphores, signal_semaphores);
  ASSERT_EQ(MAGMA_STATUS_OK, device_->SubmitBatch(std::move(batch), false /* do_flush */).get());

  constexpr uint64_t kTimeoutMs = 1000;
  ASSERT_EQ(MAGMA_STATUS_OK, semaphore->Wait(kTimeoutMs).get());

  // Submit a context state buffer in the same address space.
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x30000,
  };
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(default_context(), buffer_desc, csb1->ExecResource()));

  // The context state buffer should be executed.
  ASSERT_NO_FATAL_FAILURE(csb1->WaitForCompletion());
  ASSERT_EQ(device_->num_events_completed_, 3u);  // 1 event, context state buffer, command buffer
}

TEST_F(TestExec, SubmitContextStateBufferMultipleContexts) {
  // Create contexts with the same default address space.
  auto context_a = MsdVsiContext::Create(default_connection(), default_address_space(),
                                         device_->GetRingbuffer());
  ASSERT_NE(context_a, nullptr);
  auto client_a =
      std::make_unique<Client>(default_connection(), context_a, default_address_space());

  auto context_b = MsdVsiContext::Create(default_connection(), default_address_space(),
                                         device_->GetRingbuffer());
  ASSERT_NE(context_b, nullptr);
  auto client_b =
      std::make_unique<Client>(default_connection(), context_b, default_address_space());

  auto context_c = MsdVsiContext::Create(default_connection(), default_address_space(),
                                         device_->GetRingbuffer());
  ASSERT_NE(context_c, nullptr);
  auto client_c =
      std::make_unique<Client>(default_connection(), context_c, default_address_space());

  // Allocate the context state buffers before starting the device thread,
  // as this needs to allocate interrupt events.
  std::unique_ptr<FakeContextStateBuffer> csb_a1;
  std::unique_ptr<FakeContextStateBuffer> csb_b1;
  std::unique_ptr<FakeContextStateBuffer> csb_b2;
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, context_a, 0x10000, &csb_a1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, context_b, 0x30000, &csb_b1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, context_b, 0x20000, &csb_b2));

  device_->StartDeviceThread();

  // Submit from context_a with csb_a1.
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x40000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context_a, buffer_desc, csb_a1->ExecResource()));

  // Submit from context_b with csb_b1.
  buffer_desc.gpu_addr = 0x50000;
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context_b, buffer_desc, csb_b1->ExecResource()));

  // Submit from context_c with no CSB.
  buffer_desc.gpu_addr = 0x60000;
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context_c, buffer_desc));

  // Switch back to context_b and submit with csb_b2.
  buffer_desc.gpu_addr = 0x70000;
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(context_b, buffer_desc, csb_b2->ExecResource()));

  // All context state buffers should be executed.
  ASSERT_NO_FATAL_FAILURE(csb_a1->WaitForCompletion());
  ASSERT_NO_FATAL_FAILURE(csb_b1->WaitForCompletion());
  ASSERT_NO_FATAL_FAILURE(csb_b2->WaitForCompletion());
  ASSERT_EQ(device_->num_events_completed_, 7u);  // 3 context state buffers and 4 command buffers
}

TEST_F(TestExec, SubmitContextStateBufferMultipleAddressSpaces) {
  std::unique_ptr<Client> client_a;
  constexpr uint32_t kClientAIndex = 2;
  ASSERT_NO_FATAL_FAILURE(CreateClient(kClientAIndex, &client_a));

  std::unique_ptr<Client> client_b;
  constexpr uint32_t kClientBIndex = 3;
  ASSERT_NO_FATAL_FAILURE(CreateClient(kClientBIndex, &client_b));

  // Allocate the context state buffers before starting the device thread,
  // as this needs to allocate interrupt events.
  std::unique_ptr<FakeContextStateBuffer> csb_a1;
  std::unique_ptr<FakeContextStateBuffer> csb_a2;
  std::unique_ptr<FakeContextStateBuffer> csb_b1;
  std::unique_ptr<FakeContextStateBuffer> csb_b2;
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, client_a->context, 0x10000, &csb_a1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, client_a->context, 0x20000, &csb_a2));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, client_b->context, 0x30000, &csb_b1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, client_b->context, 0x40000, &csb_b2));

  device_->StartDeviceThread();

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x50000,
  };

  // Submit from client A.
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(client_a->context, buffer_desc, csb_a1->ExecResource()));

  // Switch to client B.
  buffer_desc.gpu_addr = 0x60000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(client_b->context, buffer_desc, csb_b1->ExecResource()));

  buffer_desc.gpu_addr = 0x70000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(client_b->context, buffer_desc, csb_b2->ExecResource()));

  // Switch back to client A.
  buffer_desc.gpu_addr = 0x80000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(client_a->context, buffer_desc, csb_a2->ExecResource()));

  // We expect all context state buffers except |csb_b2| to be executed.
  ASSERT_NO_FATAL_FAILURE(csb_a1->WaitForCompletion());
  ASSERT_NO_FATAL_FAILURE(csb_a2->WaitForCompletion());
  ASSERT_NO_FATAL_FAILURE(csb_b1->WaitForCompletion());

  ASSERT_EQ(device_->num_events_completed_, 7u);  // 3 context state buffers and 4 command buffers
}

TEST_F(TestExec, BatchHasTooManyResources) {
  // Allocate the context state buffers before starting the device thread,
  // as this needs to allocate interrupt events.
  std::unique_ptr<FakeContextStateBuffer> csb1;
  std::unique_ptr<FakeContextStateBuffer> csb2;
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, default_context(), 0x10000, &csb1));
  ASSERT_NO_FATAL_FAILURE(
      FakeContextStateBuffer::CreateWithEvent(device_, default_context(), 0x20000, &csb2));

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x30000,
  };
  std::shared_ptr<MsdVsiBuffer> buffer;
  ASSERT_NO_FATAL_FAILURE(CreateAndMapBuffer(default_context(), buffer_desc.buffer_size,
                                             buffer_desc.map_page_count, buffer_desc.gpu_addr,
                                             &buffer));

  auto command_buffer = std::make_unique<magma_system_command_buffer>(magma_system_command_buffer{
      .resource_count = 3,
      .batch_buffer_resource_index = 0,
      .batch_start_offset = buffer_desc.batch_offset,
      .wait_semaphore_count = 0,
      .signal_semaphore_count = 0,
  });
  std::vector<CommandBuffer::ExecResource> resources;
  resources.emplace_back(
      CommandBuffer::ExecResource{.buffer = buffer, .offset = 0, .length = buffer_desc.data_size});
  resources.emplace_back(csb1->ExecResource());
  resources.emplace_back(csb2->ExecResource());

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  auto batch = CommandBuffer::Create(default_context(), 0, std::move(command_buffer),
                                     std::move(resources), std::move(signal_semaphores));
  ASSERT_EQ(batch, nullptr);
}
