// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_command_buffer.h"

#include <gtest/gtest.h>
#include <helper/command_buffer_helper.h>
#include <helper/platform_device_helper.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "command_buffer.h"
#include "msd_intel_context.h"
#include "msd_intel_device.h"
#include "ppgtt.h"

using AllocatingAddressSpace = FakeAllocatingAddressSpace<GpuMapping, AddressSpace>;

class Test {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  static std::unique_ptr<Test> Create() { return std::unique_ptr<Test>(new Test()); }

  MsdIntelDevice* device() { return MsdIntelDevice::cast(helper_->dev()->msd_dev()); }

  std::shared_ptr<AddressSpace> exec_address_space() {
    auto context = MsdIntelAbiContext::cast(helper_->ctx())->ptr();
    return context->exec_address_space();
  }

  void TestMapUnmapResourcesGpu() {
    CreateCommandBuffer();

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto addr_space =
        std::make_shared<AllocatingAddressSpace>(address_space_owner.get(), 0, 1024 * PAGE_SIZE);

    {
      std::vector<MagmaSystemBuffer*>& resources = helper_->resources();
      for (auto& resource : resources) {
        std::shared_ptr<GpuMapping> mapping = addr_space->MapBufferGpu(
            addr_space, MsdIntelAbiBuffer::cast(resource->msd_buf())->ptr());
        ASSERT_TRUE(mapping);
        EXPECT_TRUE(addr_space->AddMapping(mapping));
      }
    }

    std::vector<std::shared_ptr<GpuMapping>> mappings;
    ASSERT_TRUE(TestCommandBuffer::MapResourcesGpu(cmd_buf_.get(), addr_space, mappings));

    uint32_t i = 0;
    for (auto& map : mappings) {
      gpu_addr_t addr = map->gpu_addr();
      EXPECT_TRUE(addr_space->is_allocated(addr));
      EXPECT_FALSE(addr_space->is_clear(addr));
      EXPECT_GE(addr_space->allocated_size(addr), helper_->resources()[i++]->size());
    }

    TestCommandBuffer::UnmapResourcesGpu(cmd_buf_.get());

    for (auto& map : mappings) {
      gpu_addr_t addr = map->gpu_addr();

      std::vector<std::shared_ptr<GpuMapping>> mappings;
      addr_space->ReleaseBuffer(map->buffer()->platform_buffer(), &mappings);
      EXPECT_EQ(1u, mappings.size());
      EXPECT_EQ(2u, map.use_count());
      mappings.clear();
      EXPECT_EQ(1u, map.use_count());
      EXPECT_TRUE(addr_space->is_allocated(addr));
      map.reset();
      EXPECT_FALSE(addr_space->is_allocated(addr));
    }
  }

  void TestPrepareForExecution() {
    uint32_t batch_start_offset = 0x10;
    helper_->abi_cmd_buf()->batch_start_offset = batch_start_offset;

    CreateCommandBuffer();

    {
      gpu_addr_t gpu_addr = 0;
      std::vector<MagmaSystemBuffer*>& resources = helper_->resources();
      for (auto& resource : resources) {
        auto buffer = MsdIntelAbiBuffer::cast(resource->msd_buf())->ptr();
        std::shared_ptr<GpuMapping> mapping;
        EXPECT_TRUE(AddressSpace::MapBufferGpu(exec_address_space(), buffer, gpu_addr, 0,
                                               buffer->platform_buffer()->size() / PAGE_SIZE,
                                               &mapping));
        ASSERT_TRUE(mapping);
        EXPECT_TRUE(exec_address_space()->AddMapping(mapping));
        gpu_addr += buffer->platform_buffer()->size() + PerProcessGtt::ExtraPageCount() * PAGE_SIZE;
      }
    }

    auto device = MsdIntelDevice::cast(helper_->dev()->msd_dev());
    auto engine = TestCommandBuffer::render_engine(device);

    ASSERT_TRUE(cmd_buf_->PrepareForExecution());

    auto context = cmd_buf_->GetContext().lock();
    ASSERT_NE(context, nullptr);
    ClientContext* ctx = static_cast<ClientContext*>(context.get());
    ASSERT_NE(ctx, nullptr);

    EXPECT_TRUE(TestCommandBuffer::InitContextForRender(device, context.get()));

    gpu_addr_t gpu_addr;
    EXPECT_TRUE(cmd_buf_->GetGpuAddress(&gpu_addr));
    EXPECT_EQ(batch_start_offset, gpu_addr & (PAGE_SIZE - 1));

    // Check that context is initialized correctly
    EXPECT_TRUE(ctx->IsInitializedForEngine(engine->id()));
    EXPECT_NE(ctx->get_ringbuffer(engine->id()), nullptr);
    EXPECT_NE(ctx->get_context_buffer(engine->id()), nullptr);

    // Check that context is mapped correctly
    gpu_addr_t addr;
    EXPECT_TRUE(ctx->GetGpuAddress(engine->id(), &addr));
    EXPECT_NE(addr, kInvalidGpuAddr);
    EXPECT_TRUE(ctx->GetRingbufferGpuAddress(engine->id(), &addr));
    EXPECT_NE(addr, kInvalidGpuAddr);
    cmd_buf_.reset();
  }

  void TestExecute() {
    CreateCommandBuffer();

    gpu_addr_t gpu_addr = 0;
    {
      std::vector<MagmaSystemBuffer*>& resources = helper_->resources();
      for (auto& resource : resources) {
        auto buffer = MsdIntelAbiBuffer::cast(resource->msd_buf())->ptr();
        std::shared_ptr<GpuMapping> mapping;
        EXPECT_TRUE(AddressSpace::MapBufferGpu(exec_address_space(), buffer, gpu_addr, 0,
                                               buffer->platform_buffer()->size() / PAGE_SIZE,
                                               &mapping));
        ASSERT_TRUE(mapping);
        EXPECT_TRUE(exec_address_space()->AddMapping(mapping));
        gpu_addr += buffer->platform_buffer()->size() + PerProcessGtt::ExtraPageCount() * PAGE_SIZE;
      }
    }

    // Create target buffer and mapping
    auto buffer = std::shared_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
    std::shared_ptr<GpuMapping> target_buffer_mapping;
    EXPECT_TRUE(AddressSpace::MapBufferGpu(exec_address_space(), buffer, gpu_addr, 0,
                                           buffer->platform_buffer()->size() / PAGE_SIZE,
                                           &target_buffer_mapping));
    ASSERT_TRUE(target_buffer_mapping);
    EXPECT_TRUE(exec_address_space()->AddMapping(target_buffer_mapping));

    void* target_cpu_addr;
    ASSERT_TRUE(target_buffer_mapping->buffer()->platform_buffer()->MapCpu(&target_cpu_addr));

    gpu_addr_t target_gpu_addr = target_buffer_mapping->gpu_addr();
    DLOG("target_gpu_addr 0x%lx", target_gpu_addr);
    *reinterpret_cast<uint32_t*>(target_cpu_addr) = 0;

    auto batch_buf_index = TestCommandBuffer::batch_buffer_resource_index(cmd_buf_.get());
    auto batch_res = TestCommandBuffer::exec_resources(cmd_buf_.get())[batch_buf_index];
    void* batch_cpu_addr;

    ASSERT_TRUE(batch_res.buffer->platform_buffer()->MapCpu(&batch_cpu_addr));
    uint32_t expected_val = 0xdeadbeef;
    uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

    static constexpr uint32_t kDwordCount = 4;
    static constexpr bool kUseGlobalGtt = false;
    // store dword
    *batch_ptr++ = (0x20 << 23) | (kDwordCount - 2) | (kUseGlobalGtt ? 1 << 22 : 0);
    *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
    *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);
    *batch_ptr++ = expected_val;

    // batch end
    *batch_ptr++ = (0xA << 23);

    TestCommandBuffer::StartDeviceThread(device());

    cmd_buf_.reset();
    EXPECT_TRUE(helper_->ExecuteAndWait());

    uint32_t target_val = *reinterpret_cast<uint32_t*>(target_cpu_addr);
    EXPECT_EQ(target_val, expected_val);
  }

 private:
  void CreateCommandBuffer() {
    cmd_buf_ = CommandBuffer::Create(
        MsdIntelAbiContext::cast(helper_->ctx())->ptr(), helper_->abi_cmd_buf(),
        helper_->abi_resources(), helper_->msd_resources().data(), helper_->msd_wait_semaphores(),
        helper_->msd_signal_semaphores());
  }

  Test() {
    auto platform_device = TestPlatformPciDevice::GetInstance();
    if (!platform_device)
      DLOG("TestCommandBuffer: No platform device");
    DLOG("creating helper");
    helper_ = CommandBufferHelper::Create(platform_device);
  }

  std::unique_ptr<MagmaSystemBuffer> buffer_;
  std::unique_ptr<CommandBuffer> cmd_buf_;
  std::unique_ptr<CommandBufferHelper> helper_;
};

TEST(CommandBuffer, MapUnmapResourcesGpu) { ::Test::Create()->TestMapUnmapResourcesGpu(); }

TEST(CommandBuffer, PrepareForExecution) { ::Test::Create()->TestPrepareForExecution(); }

TEST(CommandBuffer, Execute) { ::Test::Create()->TestExecute(); }
