// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "render_init_batch.h"
#include "gtest/gtest.h"

class TestRenderInitBatch {
public:
    void Init()
    {
        uint32_t batch_size = RenderInitBatch::Size();

        std::unique_ptr<RenderInitBatch> batch(new RenderInitBatch());

        uint64_t base = 0x10000;
        std::unique_ptr<MockAddressSpace> address_space(
            new MockAddressSpace(base, magma::round_up(batch_size, PAGE_SIZE)));

        {
            std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(batch_size));
            ASSERT_TRUE(buffer);

            void* addr;
            ASSERT_TRUE(buffer->platform_buffer()->MapCpu(&addr));

            // Set buffer to a known pattern
            memset(addr, 0xFF, buffer->platform_buffer()->size());

            EXPECT_TRUE(buffer->platform_buffer()->UnmapCpu());

            // Hand off the buffer
            EXPECT_TRUE(batch->Init(std::move(buffer), address_space.get()));
        }

        gpu_addr_t gpu_addr = batch->GetGpuAddress();
        EXPECT_EQ(gpu_addr, base);

        void* addr;
        ASSERT_TRUE(batch->buffer()->platform_buffer()->MapCpu(&addr));

        auto entry = reinterpret_cast<uint32_t*>(addr);

        // Check relocations
        for (unsigned int i = 0; i < RenderInitBatch::RelocationCount(); i++) {
            uint32_t index = RenderInitBatch::relocs_[i] >> 2;
            uint64_t val = entry[index + 1];
            val = (val << 32) | entry[index];
            EXPECT_EQ(val, gpu_addr + RenderInitBatch::batch_[index]);
            // Remove reloc for following memcmp
            entry[index] = RenderInitBatch::batch_[index];
            entry[index + 1] = RenderInitBatch::batch_[index + 1];
        }

        // Check everything else
        EXPECT_EQ(memcmp(addr, RenderInitBatch::batch_, RenderInitBatch::Size()), 0);

        EXPECT_TRUE(batch->buffer()->platform_buffer()->UnmapCpu());
    }
};

TEST(RenderInitBatch, Init)
{
    TestRenderInitBatch test;
    test.Init();
}
