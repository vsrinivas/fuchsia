// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "engine_command_streamer.h"
#include "gtest/gtest.h"

class TestContext {
public:
    static MsdIntelBuffer* get_buffer(MsdIntelContext* context, EngineCommandStreamerId id)
    {
        return context->get_buffer(id);
    }
};

namespace {

class Device {
public:
    void Init()
    {
        context_ = std::unique_ptr<MsdIntelContext>(new MsdIntelContext());
        render_cs_ =
            std::unique_ptr<RenderEngineCommandStreamer>(new RenderEngineCommandStreamer());

        auto buffer = TestContext::get_buffer(context_.get(), RENDER_COMMAND_STREAMER);
        EXPECT_EQ(buffer, nullptr);

        bool ret = render_cs_->InitContext(context_.get());
        EXPECT_TRUE(ret);

        buffer = TestContext::get_buffer(context_.get(), RENDER_COMMAND_STREAMER);
        EXPECT_NE(buffer, nullptr);
        EXPECT_EQ(buffer->platform_buffer()->size(), PAGE_SIZE * 20ul);
    }

private:
    std::unique_ptr<RenderEngineCommandStreamer> render_cs_;
    std::unique_ptr<MsdIntelContext> context_;
};

} // namespace

TEST(RenderEngineCommandStreamer, Init)
{
    {
        Device device;
        device.Init();
    }
}
