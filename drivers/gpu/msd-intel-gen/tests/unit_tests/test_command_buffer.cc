// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "gpu_mapping_cache.h"
#include "helper/command_buffer_helper.h"
#include "helper/platform_device_helper.h"
#include "mock/mock_address_space.h"
#include "msd_intel_context.h"
#include "msd_intel_device.h"
#include "gtest/gtest.h"

class TestCommandBuffer {
public:
    static std::unique_ptr<TestCommandBuffer> Create()
    {
        return std::unique_ptr<TestCommandBuffer>(new TestCommandBuffer());
    }

    MsdIntelDevice* device() { return MsdIntelDevice::cast(helper_->dev()->msd_dev()); }

    std::shared_ptr<AddressSpace> exec_address_space()
    {
        auto context = MsdIntelAbiContext::cast(helper_->ctx())->ptr();
        return context->exec_address_space();
    }

    void TestMapUnmapResourcesGpu()
    {
        auto addr_space =
            std::shared_ptr<MockAddressSpace>(new MockAddressSpace(0, 1024 * PAGE_SIZE));

        std::vector<std::shared_ptr<GpuMapping>> mappings;
        ASSERT_TRUE(cmd_buf_->MapResourcesGpu(addr_space, mappings));

        uint32_t i = 0;
        gpu_addr_t addr;
        for (auto& map : mappings) {
            addr = map->gpu_addr();
            EXPECT_TRUE(addr_space->is_allocated(addr));
            EXPECT_FALSE(addr_space->is_clear(addr));
            EXPECT_GE(addr_space->allocated_size(addr), helper_->resources()[i++]->size());
        }

        cmd_buf_->UnmapResourcesGpu();

        for (auto& map : mappings) {
            addr = map->gpu_addr();
            map.reset();
            EXPECT_FALSE(addr_space->is_allocated(addr));
        }
    }

    void TestPatchRelocations()
    {
        auto addr_space =
            std::shared_ptr<MockAddressSpace>(new MockAddressSpace(0, 1024 * PAGE_SIZE));

        auto batch_buf_index = cmd_buf_->batch_buffer_resource_index();
        auto batch_res = cmd_buf_->exec_resources_[batch_buf_index];
        void* batch_buf_virt_addr = 0;
        ASSERT_TRUE(batch_res.buffer->platform_buffer()->MapCpu(&batch_buf_virt_addr));
        auto batch_buf_data = (uint32_t*)batch_buf_virt_addr;

        // Clear the relocations to be sure
        auto batch_buf_resource = &cmd_buf_->resource(batch_buf_index);
        for (uint32_t i = 0; i < batch_buf_resource->num_relocations(); i++) {
            auto relocation = batch_buf_resource->relocation(i);

            uint32_t dword_offset = relocation->offset / sizeof(uint32_t);
            batch_buf_data[dword_offset] = 0xdeadbeef;
            dword_offset++;
            batch_buf_data[dword_offset] = 0xdeadbeef;
        }

        // do the relocation foo
        std::vector<std::shared_ptr<GpuMapping>> mappings;
        ASSERT_TRUE(cmd_buf_->MapResourcesGpu(addr_space, mappings));
        ASSERT_TRUE(cmd_buf_->PatchRelocations(mappings));

        // check that we foo'd it correctly
        for (uint32_t i = 0; i < batch_buf_resource->num_relocations(); i++) {
            auto relocation = batch_buf_resource->relocation(i);
            gpu_addr_t target_gpu_address = mappings[relocation->target_resource_index]->gpu_addr();
            auto expected_gpu_addr = target_gpu_address + relocation->target_offset;
            uint32_t dword_offset = relocation->offset / sizeof(uint32_t);
            EXPECT_EQ(magma::lower_32_bits(expected_gpu_addr), batch_buf_data[dword_offset]);
            dword_offset++;
            EXPECT_EQ(magma::upper_32_bits(expected_gpu_addr), batch_buf_data[dword_offset]);
        }
    }

    void TestPrepareForExecution()
    {
        auto engine = MsdIntelDevice::cast(helper_->dev()->msd_dev())->render_engine_cs();
        auto address_space = exec_address_space();

        uint32_t batch_start_offset = 0x10;
        helper_->abi_cmd_buf()->batch_start_offset = batch_start_offset;

        ASSERT_TRUE(cmd_buf_->PrepareForExecution(engine, address_space));

        auto context = cmd_buf_->GetContext().lock();
        ASSERT_NE(context, nullptr);
        ClientContext* ctx = static_cast<ClientContext*>(context.get());
        ASSERT_NE(ctx, nullptr);

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

    void TestExecute()
    {
        auto context = MsdIntelAbiContext::cast(helper_->ctx())->ptr();
        auto addr_space = exec_address_space();

        auto target_buffer_mapping =
            AddressSpace::MapBufferGpu(addr_space, MsdIntelBuffer::Create(PAGE_SIZE), PAGE_SIZE);
        ASSERT_NE(target_buffer_mapping, nullptr);

        void* target_cpu_addr;
        ASSERT_TRUE(target_buffer_mapping->buffer()->platform_buffer()->MapCpu(&target_cpu_addr));

        gpu_addr_t target_gpu_addr = target_buffer_mapping->gpu_addr();
        DLOG("target_gpu_addr 0x%lx", target_gpu_addr);
        *reinterpret_cast<uint32_t*>(target_cpu_addr) = 0;

        auto batch_buf_index = cmd_buf_->batch_buffer_resource_index();
        auto batch_res = cmd_buf_->exec_resources_[batch_buf_index];
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

        device()->StartDeviceThread();

        cmd_buf_.reset();
        EXPECT_TRUE(helper_->Execute());

        uint32_t target_val = *reinterpret_cast<uint32_t*>(target_cpu_addr);
        EXPECT_EQ(target_val, expected_val);
    }

private:
    TestCommandBuffer()
    {
        auto platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device)
            DLOG("TestCommandBuffer: No platform device");
        DLOG("creating helper");
        helper_ = CommandBufferHelper::Create(platform_device);
        DLOG("creating command buffer");
        cmd_buf_ =
            CommandBuffer::Create(helper_->buffer()->msd_buf(), helper_->msd_resources().data(),
                                  MsdIntelAbiContext::cast(helper_->ctx())->ptr());
        DLOG("command buffer created");
    }

    std::unique_ptr<CommandBuffer> cmd_buf_;
    std::unique_ptr<CommandBufferHelper> helper_;
};

TEST(CommandBuffer, MapUnmapResourcesGpu)
{
    TestCommandBuffer::Create()->TestMapUnmapResourcesGpu();
}

TEST(CommandBuffer, PatchRelocations) { TestCommandBuffer::Create()->TestPatchRelocations(); }

TEST(CommandBuffer, PrepareForExecution) { TestCommandBuffer::Create()->TestPrepareForExecution(); }

TEST(CommandBuffer, Execute) { TestCommandBuffer::Create()->TestExecute(); }