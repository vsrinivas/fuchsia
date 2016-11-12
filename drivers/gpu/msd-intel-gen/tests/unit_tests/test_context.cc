// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "global_context.h"
#include "mock/mock_address_space.h"
#include "msd_intel_context.h"
#include "ringbuffer.h"
#include "gtest/gtest.h"

class TestContext {
public:
    void Init()
    {
        std::weak_ptr<MsdIntelConnection> connection;
        std::unique_ptr<MsdIntelContext> context(new ClientContext(connection, nullptr));

        EXPECT_EQ(nullptr, get_buffer(context.get(), RENDER_COMMAND_STREAMER));
        EXPECT_EQ(nullptr, get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(10));
        ASSERT_NE(buffer, nullptr);
        auto expected_buffer = buffer.get();

        std::unique_ptr<Ringbuffer> ringbuffer(new Ringbuffer(MsdIntelBuffer::Create(PAGE_SIZE)));
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
        if (global)
            context = std::unique_ptr<MsdIntelContext>(new GlobalContext());
        else
            context = std::unique_ptr<MsdIntelContext>(new ClientContext(connection, nullptr));

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
        std::unique_ptr<Ringbuffer> ringbuffer(
            new Ringbuffer(std::unique_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE))));

        std::shared_ptr<AddressSpace> address_space(
            new MockAddressSpace(base, buffer->platform_buffer()->size() + ringbuffer->size()));

        context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

        // Not mapped
        EXPECT_FALSE(context->Unmap(address_space->id(), RENDER_COMMAND_STREAMER));

        gpu_addr_t gpu_addr;
        EXPECT_FALSE(context->GetRingbufferGpuAddress(RENDER_COMMAND_STREAMER, &gpu_addr));

        EXPECT_TRUE(context->Map(address_space, RENDER_COMMAND_STREAMER));
        EXPECT_TRUE(context->GetRingbufferGpuAddress(RENDER_COMMAND_STREAMER, &gpu_addr));
        EXPECT_GE(gpu_addr, base);

        // Already mapped
        EXPECT_TRUE(context->Map(address_space, RENDER_COMMAND_STREAMER));

        // Unmap
        EXPECT_TRUE(context->Unmap(address_space->id(), RENDER_COMMAND_STREAMER));

        // Already unmapped
        EXPECT_FALSE(context->Unmap(address_space->id(), RENDER_COMMAND_STREAMER));
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
