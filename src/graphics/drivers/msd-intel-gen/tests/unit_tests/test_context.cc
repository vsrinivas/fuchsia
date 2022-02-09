// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "address_space.h"
#include "magma_intel_gen_defs.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "ringbuffer.h"
#include "test_command_buffer.h"

using AllocatingAddressSpace = FakeAllocatingAddressSpace<GpuMapping, AddressSpace>;

class TestContext {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  void Init() {
    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space =
        std::make_shared<AllocatingAddressSpace>(address_space_owner.get(), 0, PAGE_SIZE);

    std::weak_ptr<MsdIntelConnection> connection;
    std::unique_ptr<MsdIntelContext> context(new MsdIntelContext(address_space, connection));

    EXPECT_EQ(nullptr, get_buffer(context.get(), RENDER_COMMAND_STREAMER));
    EXPECT_EQ(nullptr, get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(10, "test"));
    ASSERT_NE(buffer, nullptr);
    auto expected_buffer = buffer.get();

    auto ringbuffer = std::make_unique<Ringbuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
    ASSERT_NE(ringbuffer, nullptr);
    auto expected_ringbuffer = ringbuffer.get();

    context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

    EXPECT_EQ(expected_buffer, get_buffer(context.get(), RENDER_COMMAND_STREAMER));
    EXPECT_EQ(expected_ringbuffer, get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));
  }

  void Map(bool global) {
    // Arbitrary
    constexpr uint32_t base = 0x10000;

    std::weak_ptr<MsdIntelConnection> connection;
    std::unique_ptr<MsdIntelContext> context;

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
    auto ringbuffer = std::make_unique<Ringbuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"));

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<AllocatingAddressSpace>(
        address_space_owner.get(), base, buffer->platform_buffer()->size() + ringbuffer->size());

    if (global)
      context = std::unique_ptr<MsdIntelContext>(new MsdIntelContext(address_space));
    else
      context = std::unique_ptr<MsdIntelContext>(new MsdIntelContext(address_space, connection));

    context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

    // Not mapped
    EXPECT_FALSE(context->Unmap(RENDER_COMMAND_STREAMER));

    gpu_addr_t gpu_addr;
    EXPECT_FALSE(context->GetRingbufferGpuAddress(RENDER_COMMAND_STREAMER, &gpu_addr));

    EXPECT_TRUE(context->Map(address_space, RENDER_COMMAND_STREAMER));
    EXPECT_TRUE(context->GetRingbufferGpuAddress(RENDER_COMMAND_STREAMER, &gpu_addr));
    EXPECT_GE(gpu_addr, base);

    // Already mapped
    EXPECT_TRUE(context->Map(address_space, RENDER_COMMAND_STREAMER));

    // Unmap
    EXPECT_TRUE(context->Unmap(RENDER_COMMAND_STREAMER));

    // Already unmapped
    EXPECT_FALSE(context->Unmap(RENDER_COMMAND_STREAMER));
  }

  void CachedMapping() {
    // Arbitrary
    constexpr uint32_t base = 0x10000;

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE, "test"));

    auto ringbuffer = std::make_unique<Ringbuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"));

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<AllocatingAddressSpace>(
        address_space_owner.get(), base, buffer->platform_buffer()->size() + ringbuffer->size());

    auto context =
        std::make_unique<MsdIntelContext>(address_space, std::weak_ptr<MsdIntelConnection>());

    void* cpu_addr = context->GetCachedContextBufferCpuAddr(RENDER_COMMAND_STREAMER);
    EXPECT_EQ(nullptr, cpu_addr);

    context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

    cpu_addr = context->GetCachedContextBufferCpuAddr(RENDER_COMMAND_STREAMER);
    EXPECT_NE(nullptr, cpu_addr);
    // Returned address shouldn't change.
    EXPECT_EQ(cpu_addr, context->GetCachedContextBufferCpuAddr(RENDER_COMMAND_STREAMER));
  }

 private:
  static MsdIntelBuffer* get_buffer(MsdIntelContext* context, EngineCommandStreamerId id) {
    return context->get_context_buffer(id);
  }

  static Ringbuffer* get_ringbuffer(MsdIntelContext* context, EngineCommandStreamerId id) {
    return context->get_ringbuffer(id);
  }
};

TEST(MsdIntelContext, Init) {
  TestContext test;
  test.Init();
}

TEST(MsdIntelContext, ClientMap) {
  TestContext test;
  test.Map(false);
}

TEST(MsdIntelContext, CachedMapping) { TestContext().CachedMapping(); }

TEST(MsdIntelContext, GlobalMap) {
  TestContext test;
  test.Map(true);
}

struct Param {
  uint32_t command_buffer_count;
  uint32_t semaphore_count;
  uint64_t flags;
};

class MsdIntelContextSubmit : public testing::TestWithParam<Param> {
 public:
  class ConnectionOwner : public MsdIntelConnection::Owner {
   public:
    ConnectionOwner(std::function<void(std::unique_ptr<CommandBuffer> command_buffer)> callback)
        : callback_(callback) {
      address_space_owner_ = std::make_unique<TestContext::AddressSpaceOwner>();
    }

    void SubmitBatch(std::unique_ptr<MappedBatch> batch) override {
      DASSERT(batch->IsCommandBuffer());
      auto command_buffer = static_cast<CommandBuffer*>(batch.release());
      callback_(std::unique_ptr<CommandBuffer>(command_buffer));
    }

    void DestroyContext(std::shared_ptr<MsdIntelContext> client_context) override {}

    magma::PlatformBusMapper* GetBusMapper() override {
      return address_space_owner_->GetBusMapper();
    }

    std::function<void(std::unique_ptr<CommandBuffer>)> callback_;
    std::unique_ptr<magma::PlatformSemaphore> semaphore_;
    std::unique_ptr<AddressSpaceOwner> address_space_owner_;
  };
};

TEST_P(MsdIntelContextSubmit, SubmitCommandBuffer) {
  Param p = GetParam();

  DLOG("SubmitCommandBuffer command_buffer_count %u semaphore_count %u", p.command_buffer_count,
       p.semaphore_count);

  std::vector<std::unique_ptr<CommandBuffer>> submitted_command_buffers;
  auto finished_semaphore =
      std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

  auto owner =
      std::make_unique<ConnectionOwner>([&submitted_command_buffers, finished_semaphore,
                                         p](std::unique_ptr<CommandBuffer> command_buffer) {
        submitted_command_buffers.push_back(std::move(command_buffer));
        if (submitted_command_buffers.size() == p.command_buffer_count)
          finished_semaphore->Signal();
      });

  auto connection =
      std::shared_ptr<MsdIntelConnection>(MsdIntelConnection::Create(owner.get(), 0u));
  auto address_space = std::make_shared<AllocatingAddressSpace>(owner.get(), 0, PAGE_SIZE);

  auto context = std::make_shared<MsdIntelContext>(address_space, connection);

  std::vector<std::unique_ptr<CommandBuffer>> command_buffers;
  std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

  for (uint32_t i = 0; i < p.command_buffer_count; i++) {
    // Don't need a fully initialized command buffer
    std::shared_ptr<MsdIntelBuffer> command_buffer_content =
        MsdIntelBuffer::Create(PAGE_SIZE, "test");
    magma_command_buffer* command_buffer_desc;
    ASSERT_TRUE(command_buffer_content->platform_buffer()->MapCpu(
        reinterpret_cast<void**>(&command_buffer_desc)));

    command_buffer_desc->resource_count = 0;
    command_buffer_desc->batch_buffer_resource_index = 0;
    command_buffer_desc->batch_start_offset = 0;
    command_buffer_desc->wait_semaphore_count = 0;
    command_buffer_desc->signal_semaphore_count = 0;
    command_buffer_desc->flags = p.flags;

    std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
    for (uint32_t i = 0; i < p.semaphore_count; i++) {
      auto semaphore =
          std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());
      wait_semaphores.push_back(semaphore);
      semaphores.push_back(semaphore);
    }
    command_buffer_desc->wait_semaphore_count = p.semaphore_count;

    auto command_buffer =
        TestCommandBuffer::Create(command_buffer_content, context, {}, wait_semaphores, {});
    ASSERT_NE(command_buffer, nullptr);

    command_buffers.push_back(std::move(command_buffer));

    magma::Status status = context->SubmitCommandBuffer(std::move(command_buffers[i]));

    EXPECT_EQ(MAGMA_STATUS_OK, status.get());
    EXPECT_TRUE(context->GetTargetCommandStreamer());
    if (p.flags == kMagmaIntelGenCommandBufferForVideo) {
      EXPECT_EQ(*context->GetTargetCommandStreamer(), VIDEO_COMMAND_STREAMER);
    } else {
      EXPECT_EQ(*context->GetTargetCommandStreamer(), RENDER_COMMAND_STREAMER);
    }
    EXPECT_EQ(submitted_command_buffers.empty(), p.semaphore_count > 0);
  }

  for (uint32_t i = 0; i < semaphores.size(); i++) {
    semaphores[i]->Signal();
  }

  EXPECT_TRUE(finished_semaphore->Wait(5000));
  ASSERT_EQ(submitted_command_buffers.size(), command_buffers.size());

  context->Shutdown();
}

INSTANTIATE_TEST_SUITE_P(MsdIntelContextSubmit, MsdIntelContextSubmit,
                         testing::Values(Param{.command_buffer_count = 1, .semaphore_count = 0},
                                         Param{.command_buffer_count = 1, .semaphore_count = 1},
                                         Param{.command_buffer_count = 2, .semaphore_count = 1},
                                         Param{.command_buffer_count = 3, .semaphore_count = 2},
                                         Param{.command_buffer_count = 2, .semaphore_count = 5},
                                         Param{.command_buffer_count = 1,
                                               .semaphore_count = 0,
                                               .flags = kMagmaIntelGenCommandBufferForVideo}),
                         [](testing::TestParamInfo<Param> info) {
                           char name[128];
                           snprintf(name, sizeof(name),
                                    "command_buffer_count_%u_semaphore_count_%u_flags_0x%lx",
                                    info.param.command_buffer_count, info.param.semaphore_count,
                                    info.param.flags);
                           return std::string(name);
                         });
