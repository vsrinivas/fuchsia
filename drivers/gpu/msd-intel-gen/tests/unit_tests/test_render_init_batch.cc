// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "render_init_batch.h"
#include "gtest/gtest.h"

class TestRenderInitBatch {
public:
    void Init(std::unique_ptr<RenderInitBatch> batch)
    {
        uint64_t base = 0x10000;
        std::unique_ptr<MockAddressSpace> address_space(
            new MockAddressSpace(base, magma::round_up(batch->size(), PAGE_SIZE)));

        {
            std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(batch->size()));
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
        for (unsigned int i = 0; i < batch->relocation_count_; i++) {
            uint32_t index = batch->relocs_[i] >> 2;
            uint64_t val = entry[index + 1];
            val = (val << 32) | entry[index];
            EXPECT_EQ(val, gpu_addr + batch->batch_[index]);
            // Remove reloc for following memcmp
            entry[index] = batch->batch_[index];
            entry[index + 1] = batch->batch_[index + 1];
        }

        // Check everything else
        EXPECT_EQ(memcmp(addr, batch->batch_, batch->size()), 0);

        EXPECT_TRUE(batch->buffer()->platform_buffer()->UnmapCpu());
    }
};

TEST(RenderInitBatch, Init)
{
    TestRenderInitBatch test;
    test.Init(std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen8()));
    test.Init(std::unique_ptr<RenderInitBatch>(new RenderInitBatchGen9()));
}
