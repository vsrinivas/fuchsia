// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"
#include "ringbuffer.h"
#include "gtest/gtest.h"

class TestContext {
public:
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
    std::unique_ptr<MsdIntelContext> context(new MsdIntelContext());

    EXPECT_EQ(nullptr, TestContext::get_buffer(context.get(), RENDER_COMMAND_STREAMER));
    EXPECT_EQ(nullptr, TestContext::get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(10));
    ASSERT_NE(buffer, nullptr);
    auto expected_buffer = buffer.get();

    std::unique_ptr<Ringbuffer> ringbuffer(Ringbuffer::Create());
    ASSERT_NE(ringbuffer, nullptr);
    auto expected_ringbuffer = ringbuffer.get();

    context->SetEngineState(RENDER_COMMAND_STREAMER, std::move(buffer), std::move(ringbuffer));

    EXPECT_EQ(expected_buffer, TestContext::get_buffer(context.get(), RENDER_COMMAND_STREAMER));
    EXPECT_EQ(expected_ringbuffer,
              TestContext::get_ringbuffer(context.get(), RENDER_COMMAND_STREAMER));
}
