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

        void* addr;
        ret = buffer->platform_buffer()->MapCpu(&addr);
        EXPECT_TRUE(ret);

        uint32_t* state = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(addr) + PAGE_SIZE);
        EXPECT_EQ(state[1], 0x1100101Bul);
        EXPECT_EQ(state[2], 0x2244ul);
        EXPECT_EQ(state[3], 0x00090009ul);
        EXPECT_EQ(state[4], 0x2034ul);
        EXPECT_EQ(state[5], 0ul);
        EXPECT_EQ(state[6], 0x2030ul);
        EXPECT_EQ(state[7], 0ul);
        EXPECT_EQ(state[8], 0x2038ul);
        // state[9] is not set until later
        EXPECT_EQ(state[0xA], 0x203Cul);
        EXPECT_EQ(state[0xB], (31ul * PAGE_SIZE) | 1);
        EXPECT_EQ(state[0xC], 0x2168ul);
        EXPECT_EQ(state[0xD], 0ul);
        EXPECT_EQ(state[0xE], 0x2140ul);
        EXPECT_EQ(state[0xF], 0ul);
        EXPECT_EQ(state[0x10], 0x2110ul);
        EXPECT_EQ(state[0x11], 1ul << 5);
        EXPECT_EQ(state[0x12], 0x211Cul);
        EXPECT_EQ(state[0x13], 0ul);
        EXPECT_EQ(state[0x14], 0x2114ul);
        EXPECT_EQ(state[0x15], 0ul);
        EXPECT_EQ(state[0x16], 0x2118ul);
        EXPECT_EQ(state[0x17], 0ul);
        EXPECT_EQ(state[0x18], 0x21C0ul);
        EXPECT_EQ(state[0x19], 0ul);
        EXPECT_EQ(state[0x1A], 0x21C4ul);
        EXPECT_EQ(state[0x1B], 0ul);
        EXPECT_EQ(state[0x1C], 0x21C8ul);
        EXPECT_EQ(state[0x1D], 0ul);
        EXPECT_EQ(state[0x1E], 0x23A8ul);
        EXPECT_EQ(state[0x1F], 0ul);
        EXPECT_EQ(state[0x21], 0x11001011ul);
        EXPECT_EQ(state[0x24], 0x228Cul);
        // TODO(MA-64) - check ppgtt pdp addresses
        // EXPECT_EQ(state[0x25], pdp3_upper);
        EXPECT_EQ(state[0x26], 0x2288ul);
        // EXPECT_EQ(state[0x27], pdp3_lower);
        EXPECT_EQ(state[0x28], 0x2284ul);
        // EXPECT_EQ(state[0x29], pdp2_upper);
        EXPECT_EQ(state[0x2A], 0x2280ul);
        // EXPECT_EQ(state[0x2B], pdp2_lower);
        EXPECT_EQ(state[0x2C], 0x227Cul);
        // EXPECT_EQ(state[0x2D], pdp1_upper);
        EXPECT_EQ(state[0x2E], 0x2278ul);
        // EXPECT_EQ(state[0x2F], pdp1_lower);
        EXPECT_EQ(state[0x30], 0x2274ul);
        // EXPECT_EQ(state[0x31], pdp0_upper);
        EXPECT_EQ(state[0x32], 0x2270ul);
        // EXPECT_EQ(state[0x33], pdp0_lower);
        EXPECT_EQ(state[0x41], 0x11000001ul);
        EXPECT_EQ(state[0x42], 0x20C8ul);
        EXPECT_EQ(state[0x43], 0ul);
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
