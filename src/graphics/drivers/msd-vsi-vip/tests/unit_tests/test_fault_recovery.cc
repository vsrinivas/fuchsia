// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "test_command_buffer.h"

class TestFaultRecovery : public TestCommandBuffer {};

TEST_F(TestFaultRecovery, ManyBatches) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

  // Submit a batch that jumps to an invalid address.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);
  constexpr uint32_t fault_addr = 0x50000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(default_context(), buffer_desc, semaphore->Clone(), fault_addr));
  semaphores.push_back(std::move(semaphore));

  // Submit more batches from the same context.
  for (unsigned int i = 0; i < MsdVsiDevice::kNumEvents; i++) {
    buffer_desc.gpu_addr += magma::page_size();
    auto semaphore = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphore, nullptr);
    ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(default_context(), buffer_desc,
                                                  semaphore->Clone(),
                                                  std::optional<uint32_t>{} /* fault_addr */));
    semaphores.push_back(std::move(semaphore));
  }

  // Begin the processing of batches.
  device_->StartDeviceThread();

  // Wait for all batches to be signalled.
  constexpr uint64_t kTimeoutMs = 1000;
  for (unsigned int i = 0; i < semaphores.size(); i++) {
    ASSERT_EQ(MAGMA_STATUS_OK, semaphores[i]->Wait(kTimeoutMs).get());
  }
  ASSERT_TRUE(default_context()->killed());
}

TEST_F(TestFaultRecovery, MultipleContexts) {
  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 8,
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

  // Submit a batch that jumps to an invalid address.
  auto semaphore = magma::PlatformSemaphore::Create();
  ASSERT_NE(semaphore, nullptr);
  constexpr uint32_t fault_addr = 0x50000;
  ASSERT_NO_FATAL_FAILURE(
      CreateAndSubmitBuffer(default_context(), buffer_desc, semaphore->Clone(), fault_addr));
  semaphores.push_back(std::move(semaphore));

  // Create new clients that will submit valid batches.
  std::vector<std::unique_ptr<Client>> clients;
  constexpr uint32_t kClientsCount = 5;
  constexpr uint32_t kClientStartAddressSpaceIndex = 10;

  for (unsigned int i = 0; i < kClientsCount; i++) {
    std::unique_ptr<Client> client;
    ASSERT_NO_FATAL_FAILURE(CreateClient(kClientStartAddressSpaceIndex + i, &client));

    semaphore = magma::PlatformSemaphore::Create();
    ASSERT_NE(semaphore, nullptr);
    std::shared_ptr<MsdVsiBuffer> buffer;
    buffer_desc.gpu_addr += magma::page_size();
    ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBuffer(client->context, buffer_desc, semaphore->Clone(),
                                                  std::optional<uint32_t>{} /* fault_addr */,
                                                  std::nullopt /* csb */, &buffer));
    semaphores.push_back(std::move(semaphore));

    clients.emplace_back(std::move(client));
  }
  // Begin the processing of batches.
  device_->StartDeviceThread();

  // Wait for all batches to be signalled.
  constexpr uint64_t kTimeoutMs = 1000;
  for (unsigned int i = 0; i < semaphores.size(); i++) {
    ASSERT_EQ(MAGMA_STATUS_OK, semaphores[i]->Wait(kTimeoutMs).get());
  }

  ASSERT_TRUE(default_context()->killed());

  // Ensure we can queue and complete new batches.
  ASSERT_FALSE(clients[0]->context->killed());
  buffer_desc.gpu_addr += magma::page_size();
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBufferWaitCompletion(clients[0]->context, buffer_desc));
}
