// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "msd_intel_context.h"
#include "ringbuffer.h"
#include "gtest/gtest.h"

class TestContext {
public:
    void Init()
    {
        std::unique_ptr<MsdIntelContext> context(new MsdIntelContext());

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

    void MapGpu()
    {
        std::unique_ptr<MsdIntelContext> context(new MsdIntelContext());

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
        std::unique_ptr<Ringbuffer> ringbuffer(
            new Ringbuffer(std::unique_ptr<MsdIntelBuffer>(MsdIntelBuffer::Create(PAGE_SIZE))));

        std::unique_ptr<AddressSpace> address_space(
            new MockAddressSpace(0x10000, buffer->platform_buffer()->size() + ringbuffer->size()));

        context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

        EXPECT_FALSE(context->UnmapGpu(address_space.get(), RENDER_COMMAND_STREAMER));
        EXPECT_TRUE(context->MapGpu(address_space.get(), RENDER_COMMAND_STREAMER));
        // Already mapped
        EXPECT_TRUE(context->MapGpu(address_space.get(), RENDER_COMMAND_STREAMER));
        EXPECT_TRUE(context->UnmapGpu(address_space.get(), RENDER_COMMAND_STREAMER));
        EXPECT_FALSE(context->UnmapGpu(address_space.get(), RENDER_COMMAND_STREAMER));
    }

private:
    static MsdIntelBuffer* get_buffer(MsdIntelContext* context, EngineCommandStreamerId id)
    {
        return context->get_buffer(id);
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

TEST(MsdIntelContext, MapGpu)
{
    TestContext test;
    test.MapGpu();
}
