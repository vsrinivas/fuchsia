// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "msd_intel_buffer.h"
#include "gtest/gtest.h"

class TestMsdIntelBuffer {
public:
    void CreateAndDestroy()
    {
        std::unique_ptr<MsdIntelBuffer> buffer;
        uint64_t size;

        buffer = MsdIntelBuffer::Create(size = 0);
        EXPECT_EQ(buffer, nullptr);

        buffer = MsdIntelBuffer::Create(size = 100);
        EXPECT_NE(buffer, nullptr);
        EXPECT_GE(buffer->platform_buffer()->size(), size);

        buffer = MsdIntelBuffer::Create(size = 10000);
        EXPECT_NE(buffer, nullptr);
        EXPECT_GE(buffer->platform_buffer()->size(), size);
    }

    void MapGpu(uint32_t alignment)
    {
        uint64_t base = PAGE_SIZE;
        uint64_t size = PAGE_SIZE * 10;

        std::unique_ptr<MockAddressSpace> address_space(new MockAddressSpace(base, size));

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
        ASSERT_NE(buffer, nullptr);

        EXPECT_TRUE(buffer->MapGpu(address_space.get(), alignment));

        gpu_addr_t gpu_addr;
        EXPECT_TRUE(buffer->GetGpuAddress(address_space->id(), &gpu_addr));
        if (alignment)
            EXPECT_TRUE((gpu_addr % alignment) == 0);

        EXPECT_TRUE(address_space->is_allocated(gpu_addr));
        EXPECT_FALSE(address_space->is_clear(gpu_addr));

        EXPECT_TRUE(buffer->UnmapGpu(address_space.get()));

        EXPECT_FALSE(address_space->is_allocated(gpu_addr));
        EXPECT_TRUE(address_space->is_clear(gpu_addr));
    }
};

TEST(MsdIntelBuffer, CreateAndDestroy)
{
    TestMsdIntelBuffer test;
    test.CreateAndDestroy();
}

TEST(MsdIntelBuffer, MapGpu)
{
    {
        TestMsdIntelBuffer test;
        test.MapGpu(0);
    }
    {
        TestMsdIntelBuffer test;
        test.MapGpu(8);
    }
    {
        TestMsdIntelBuffer test;
        test.MapGpu(16);
    }
    {
        TestMsdIntelBuffer test;
        test.MapGpu(64);
    }
    {
        TestMsdIntelBuffer test;
        test.MapGpu(4096);
    }
    {
        TestMsdIntelBuffer test;
        test.MapGpu(8192);
    }
}
