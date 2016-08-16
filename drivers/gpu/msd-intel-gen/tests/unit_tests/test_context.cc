// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_intel_context.h"
#include "gtest/gtest.h"

class TestContext {
public:
    static MsdIntelBuffer* get_buffer(MsdIntelContext* context, EngineCommandStreamerId id)
    {
        return context->get_buffer(id);
    }
};

TEST(MsdIntelContext, CreateAndDestroy)
{
    std::unique_ptr<MsdIntelContext> context(new MsdIntelContext());

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(10));
    auto expected = buffer.get();

    context->InitEngine(RENDER_COMMAND_STREAMER, std::move(buffer));
    EXPECT_EQ(expected, TestContext::get_buffer(context.get(), RENDER_COMMAND_STREAMER));
}
