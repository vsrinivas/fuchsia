// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "global_context.h"
#include "mock/mock_address_space.h"
#include "msd_intel_connection.h"
#include "msd_intel_context.h"
#include "ringbuffer.h"
#include "test_command_buffer.h"
#include "gtest/gtest.h"

class TestContext {
public:
    void Init()
    {
        std::weak_ptr<MsdIntelConnection> connection;
        auto address_space = std::make_shared<MockAddressSpace>(0, PAGE_SIZE);

        std::unique_ptr<MsdIntelContext> context(new ClientContext(connection, address_space));

        EXPECT_EQ(nullptr, get_buffer(context.get(), RENDER_COMMAND_STREAMER));
        EXPECT_EQ(nullptr, get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(10, "test"));
        ASSERT_NE(buffer, nullptr);
        auto expected_buffer = buffer.get();

        std::unique_ptr<Ringbuffer> ringbuffer(
            new Ringbuffer(MsdIntelBuffer::Create(PAGE_SIZE, "test")));
        ASSERT_NE(ringbuffer, nullptr);
        auto expected_ringbuffer = ringbuffer.get();

        context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

        EXPECT_EQ(expected_buffer, get_buffer(context.get(), RENDER_COMMAND_STREAMER));
        EXPECT_EQ(expected_ringbuffer, get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));
    }

    void Map(bool global)
    {
        constexpr uint32_t base = 0x10000;

        std::weak_ptr<MsdIntelConnection> connection;
        std::unique_ptr<MsdIntelContext> context;

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
        std::unique_ptr<Ringbuffer> ringbuffer(new Ringbuffer(
            std::unique_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE, "test"))));

        std::shared_ptr<AddressSpace> address_space(
            new MockAddressSpace(base, buffer->platform_buffer()->size() + ringbuffer->size()));

        if (global)
            context = std::unique_ptr<MsdIntelContext>(new GlobalContext(address_space));
        else
            context =
                std::unique_ptr<MsdIntelContext>(new ClientContext(connection, address_space));

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

    static void SubmitCommandBuffer(uint32_t command_buffer_count, uint32_t semaphore_count)
    {
        DLOG("SubmitCommandBuffer command_buffer_count %u semaphore_count %u", command_buffer_count,
             semaphore_count);

        class ConnectionOwner : public MsdIntelConnection::Owner {
        public:
            ConnectionOwner(
                std::function<void(std::unique_ptr<CommandBuffer> command_buffer)> callback)
                : callback_(callback)
            {
            }

            magma::Status
            SubmitCommandBuffer(std::unique_ptr<CommandBuffer> command_buffer) override
            {
                DLOG("command buffer received 0x%" PRIx64,
                     TestCommandBuffer::platform_buffer(command_buffer.get())->id());
                callback_(std::move(command_buffer));
                return MAGMA_STATUS_OK;
            }

            void DestroyContext(std::shared_ptr<ClientContext> client_context) override {}
            void ReleaseBuffer(std::shared_ptr<AddressSpace> address_space,
                               std::shared_ptr<MsdIntelBuffer> buffer) override
            {
            }

            std::function<void(std::unique_ptr<CommandBuffer>)> callback_;
            std::unique_ptr<magma::PlatformSemaphore> semaphore_;
        };

        std::vector<std::unique_ptr<CommandBuffer>> submitted_command_buffers;
        auto finished_semaphore =
            std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());

        auto owner = std::make_unique<ConnectionOwner>(
            [&submitted_command_buffers, finished_semaphore,
             command_buffer_count](std::unique_ptr<CommandBuffer> command_buffer) {
                submitted_command_buffers.push_back(std::move(command_buffer));
                if (submitted_command_buffers.size() == command_buffer_count)
                    finished_semaphore->Signal();
            });

        auto connection =
            std::shared_ptr<MsdIntelConnection>(MsdIntelConnection::Create(owner.get(), nullptr));
        auto address_space = std::make_shared<MockAddressSpace>(0, PAGE_SIZE);

        auto context = std::make_shared<ClientContext>(connection, address_space);

        std::vector<std::unique_ptr<CommandBuffer>> command_buffers;

        std::vector<uint64_t> command_buffer_ids;
        std::vector<std::shared_ptr<magma::PlatformSemaphore>> semaphores;

        for (uint32_t i = 0; i < command_buffer_count; i++) {
            // Don't need a fully initialized command buffer
            std::shared_ptr<MsdIntelBuffer> command_buffer_content =
                MsdIntelBuffer::Create(PAGE_SIZE, "test");
            magma_system_command_buffer* command_buffer_desc;
            ASSERT_TRUE(command_buffer_content->platform_buffer()->MapCpu(
                reinterpret_cast<void**>(&command_buffer_desc)));

            command_buffer_desc->batch_buffer_resource_index = 0;
            command_buffer_desc->batch_start_offset = 0;
            command_buffer_desc->num_resources = 0;
            command_buffer_desc->wait_semaphore_count = 0;
            command_buffer_desc->signal_semaphore_count = 0;

            std::vector<std::shared_ptr<magma::PlatformSemaphore>> wait_semaphores;
            for (uint32_t i = 0; i < semaphore_count; i++) {
                auto semaphore =
                    std::shared_ptr<magma::PlatformSemaphore>(magma::PlatformSemaphore::Create());
                wait_semaphores.push_back(semaphore);
                semaphores.push_back(semaphore);
            }
            command_buffer_desc->wait_semaphore_count = semaphore_count;

            auto command_buffer =
                TestCommandBuffer::Create(command_buffer_content, context, {}, wait_semaphores, {});
            ASSERT_NE(command_buffer, nullptr);

            command_buffers.push_back(std::move(command_buffer));
            command_buffer_ids.push_back(
                TestCommandBuffer::platform_buffer(command_buffers[i].get())->id());

            magma::Status status = context->SubmitCommandBuffer(std::move(command_buffers[i]));
            EXPECT_EQ(MAGMA_STATUS_OK, status.get());
            EXPECT_EQ(submitted_command_buffers.empty(), semaphore_count > 0);
        }

        for (uint32_t i = 0; i < semaphores.size(); i++) {
            semaphores[i]->Signal();
        }

        EXPECT_TRUE(finished_semaphore->Wait(5000));
        ASSERT_EQ(submitted_command_buffers.size(), command_buffer_ids.size());

        for (uint32_t i = 0; i < command_buffer_ids.size(); i++) {
            EXPECT_EQ(TestCommandBuffer::platform_buffer(submitted_command_buffers[i].get())->id(),
                      command_buffer_ids[i]);
        }

        context->Shutdown();
    }

private:
    static MsdIntelBuffer* get_buffer(MsdIntelContext* context, EngineCommandStreamerId id)
    {
        return context->get_context_buffer(id);
    }

    static Ringbuffer* get_ringbuffer(MsdIntelContext* context, EngineCommandStreamerId id)
    {
        return context->get_ringbuffer(id);
    }
};

TEST(MsdIntelContext, Init)
{
    TestContext test;
    test.Init();
}

TEST(MsdIntelContext, ClientMap)
{
    TestContext test;
    test.Map(false);
}

TEST(GlobalContext, GlobalMap)
{
    TestContext test;
    test.Map(true);
}

TEST(ClientContext, SubmitCommandBuffer)
{
    TestContext::SubmitCommandBuffer(1, 0);
    TestContext::SubmitCommandBuffer(1, 1);
    TestContext::SubmitCommandBuffer(2, 1);
    TestContext::SubmitCommandBuffer(3, 2);
    TestContext::SubmitCommandBuffer(2, 5);
}
