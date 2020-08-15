// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <magma_util/address_space.h>
#include <magma_util/command_buffer.h>
#include <magma_util/mapped_batch.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "platform_buffer.h"
#include "platform_semaphore.h"

using Buffer = magma::PlatformBuffer;

using Mapping = magma::GpuMapping<Buffer>;

using AddressSpace = FakeAllocatingAddressSpace<Mapping, magma::AddressSpace<Mapping>>;

class AddressSpaceOwner : public magma::AddressSpaceOwner {
 public:
  magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

  MockBusMapper bus_mapper_;
};

class Context {
 public:
  std::shared_ptr<AddressSpace> exec_address_space() {
    if (!address_space_) {
      address_space_ = std::make_shared<AddressSpace>(&address_space_owner_, 0, 4096 * 10);
    }
    return address_space_;
  }

  AddressSpaceOwner address_space_owner_;
  std::shared_ptr<AddressSpace> address_space_;
};

using CommandBuffer = magma::CommandBuffer<Context, Mapping>;

TEST(TestCommandBuffer, SequenceNumber) {
  constexpr uint64_t kConnectionId = 1234;
  auto context = std::make_shared<Context>();

  auto magma_command_buffer = std::make_unique<magma_system_command_buffer>();

  CommandBuffer command_buffer(context, kConnectionId, std::move(magma_command_buffer));

  EXPECT_TRUE(command_buffer.IsCommandBuffer());
  EXPECT_EQ(context, command_buffer.GetContext().lock());

  constexpr uint32_t kSequenceNumber = 0xabcd1234;
  command_buffer.SetSequenceNumber(kSequenceNumber);
  EXPECT_EQ(kSequenceNumber, command_buffer.GetSequenceNumber());
}

TEST(TestCommandBuffer, InitializeResources) {
  constexpr uint64_t kConnectionId = 1234;
  constexpr uint64_t kPageSize = 4096;
  auto context = std::make_shared<Context>();

  std::vector<CommandBuffer::ExecResource> resources;
  resources.push_back({std::shared_ptr<Buffer>(Buffer::Create(kPageSize, "A")), 0, kPageSize});
  resources.push_back(
      {std::shared_ptr<Buffer>(Buffer::Create(kPageSize * 2, "B")), 0, kPageSize * 2});
  resources.push_back(
      {std::shared_ptr<Buffer>(Buffer::Create(kPageSize * 3, "C")), 0, kPageSize * 3});

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  wait_semaphores.push_back(magma::PlatformSemaphore::Create());
  wait_semaphores.push_back(magma::PlatformSemaphore::Create());

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  signal_semaphores.push_back(magma::PlatformSemaphore::Create());

  {
    magma_system_command_buffer magma_command_buffer = {
        .resource_count = static_cast<uint32_t>(resources.size()) - 1,
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()),
        .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()),
    };

    CommandBuffer command_buffer(
        context, kConnectionId,
        std::make_unique<magma_system_command_buffer>(magma_command_buffer));

    EXPECT_FALSE(command_buffer.InitializeResources(resources, wait_semaphores, signal_semaphores));
  }

  {
    magma_system_command_buffer magma_command_buffer = {
        .resource_count = static_cast<uint32_t>(resources.size()),
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()) - 1,
        .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()),
    };

    CommandBuffer command_buffer(
        context, kConnectionId,
        std::make_unique<magma_system_command_buffer>(magma_command_buffer));

    EXPECT_FALSE(command_buffer.InitializeResources(resources, wait_semaphores, signal_semaphores));
  }

  {
    magma_system_command_buffer magma_command_buffer = {
        .resource_count = static_cast<uint32_t>(resources.size()),
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()),
        .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()) - 1,
    };

    CommandBuffer command_buffer(
        context, kConnectionId,
        std::make_unique<magma_system_command_buffer>(magma_command_buffer));

    EXPECT_FALSE(command_buffer.InitializeResources(resources, wait_semaphores, signal_semaphores));
  }

  {
    magma_system_command_buffer magma_command_buffer = {
        .resource_count = static_cast<uint32_t>(resources.size()),
        .batch_buffer_resource_index = 0,
        .batch_start_offset = 0,
        .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()),
        .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()),
    };

    CommandBuffer command_buffer(
        context, kConnectionId,
        std::make_unique<magma_system_command_buffer>(magma_command_buffer));

    EXPECT_TRUE(command_buffer.InitializeResources(resources, wait_semaphores, signal_semaphores));

    EXPECT_EQ(command_buffer.GetLength(),
              resources[magma_command_buffer.batch_buffer_resource_index].length);

    EXPECT_EQ(command_buffer.GetBatchBufferId(),
              resources[magma_command_buffer.batch_buffer_resource_index].buffer->id());
  }
}

TEST(TestCommandBuffer, PrepareForExecution) {
  constexpr uint64_t kConnectionId = 1234;
  auto context = std::make_shared<Context>();

  std::vector<CommandBuffer::ExecResource> resources;
  resources.push_back({std::shared_ptr<Buffer>(Buffer::Create(4096, "A")), 0, 0});
  resources.push_back({std::shared_ptr<Buffer>(Buffer::Create(4096, "B")), 0, 0});
  resources.push_back({std::shared_ptr<Buffer>(Buffer::Create(4096, "C")), 0, 0});

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
  wait_semaphores.push_back(magma::PlatformSemaphore::Create());
  wait_semaphores.push_back(magma::PlatformSemaphore::Create());

  std::vector<std::shared_ptr<magma::PlatformSemaphore>> signal_semaphores;
  signal_semaphores.push_back(magma::PlatformSemaphore::Create());
  signal_semaphores.push_back(magma::PlatformSemaphore::Create());
  signal_semaphores.push_back(magma::PlatformSemaphore::Create());

  magma_system_command_buffer magma_command_buffer = {
      .resource_count = static_cast<uint32_t>(resources.size()),
      .batch_buffer_resource_index = 0,
      .batch_start_offset = 0,
      .wait_semaphore_count = static_cast<uint32_t>(wait_semaphores.size()),
      .signal_semaphore_count = static_cast<uint32_t>(signal_semaphores.size()),
  };

  auto command_buffer = std::make_unique<CommandBuffer>(
      context, kConnectionId, std::make_unique<magma_system_command_buffer>(magma_command_buffer));

  EXPECT_TRUE(command_buffer->InitializeResources(resources, wait_semaphores, signal_semaphores));

  EXPECT_FALSE(command_buffer->PrepareForExecution());  // No mappings found

  EXPECT_TRUE(context->exec_address_space()->AddMapping(
      AddressSpace::MapBufferGpu(context->exec_address_space(), resources[0].buffer)));
  EXPECT_TRUE(context->exec_address_space()->AddMapping(
      AddressSpace::MapBufferGpu(context->exec_address_space(), resources[1].buffer)));
  EXPECT_TRUE(context->exec_address_space()->AddMapping(
      AddressSpace::MapBufferGpu(context->exec_address_space(), resources[2].buffer)));

  EXPECT_TRUE(command_buffer->PrepareForExecution());

  command_buffer.reset();

  for (auto& sem : signal_semaphores) {
    EXPECT_TRUE(sem->Wait(1000));
  }
}
