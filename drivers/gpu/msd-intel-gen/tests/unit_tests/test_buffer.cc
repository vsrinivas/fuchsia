// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock/mock_address_space.h"
#include "msd_intel_buffer.h"
#include "gtest/gtest.h"

class TestMsdIntelBuffer {
public:
    static void CreateAndDestroy()
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

    static void MapGpu(uint32_t alignment)
    {
        uint64_t base = PAGE_SIZE;
        uint64_t size = PAGE_SIZE * 10;

        std::shared_ptr<MockAddressSpace> address_space(new MockAddressSpace(base, size));

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
        ASSERT_NE(buffer, nullptr);

        auto mapping = AddressSpace::MapBufferGpu(address_space, std::move(buffer), alignment);
        ASSERT_NE(mapping, nullptr);

        gpu_addr_t gpu_addr = mapping->gpu_addr();
        if (alignment)
            EXPECT_TRUE((gpu_addr % alignment) == 0);

        EXPECT_TRUE(address_space->is_allocated(gpu_addr));
        EXPECT_FALSE(address_space->is_clear(gpu_addr));

        mapping.reset();

        EXPECT_FALSE(address_space->is_allocated(gpu_addr));
        EXPECT_TRUE(address_space->is_clear(gpu_addr));
    }

    static void SharedMapping()
    {
        std::shared_ptr<MockAddressSpace> address_space(new MockAddressSpace(0, PAGE_SIZE * 5));
        ASSERT_EQ(address_space->id(), ADDRESS_SPACE_GTT);

        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE));
        ASSERT_NE(buffer, nullptr);

        std::unique_ptr<GpuMapping> unique_mapping =
            AddressSpace::MapBufferGpu(address_space, buffer, 0);
        ASSERT_NE(unique_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        std::shared_ptr<GpuMapping> shared_mapping = buffer->FindBufferMapping(ADDRESS_SPACE_PPGTT);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->FindBufferMapping(ADDRESS_SPACE_GTT);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->ShareBufferMapping(std::move(unique_mapping));
        EXPECT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(ADDRESS_SPACE_GTT);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        {
            std::shared_ptr<GpuMapping> copy =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, 0);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        shared_mapping.reset();

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(ADDRESS_SPACE_GTT);
            EXPECT_EQ(copy, nullptr);
        }

        shared_mapping = AddressSpace::GetSharedGpuMapping(address_space, buffer, 0);
        ASSERT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(ADDRESS_SPACE_GTT);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        {
            std::shared_ptr<GpuMapping> copy =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, 0);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }
    }
};

TEST(MsdIntelBuffer, CreateAndDestroy)
{
    TestMsdIntelBuffer::CreateAndDestroy();
}

TEST(MsdIntelBuffer, MapGpu)
{
    TestMsdIntelBuffer::MapGpu(0);
    TestMsdIntelBuffer::MapGpu(8);
    TestMsdIntelBuffer::MapGpu(16);
    TestMsdIntelBuffer::MapGpu(64);
    TestMsdIntelBuffer::MapGpu(4096);
    TestMsdIntelBuffer::MapGpu(8192);
}

TEST(MsdIntelBuffer, SharedMapping) { TestMsdIntelBuffer::SharedMapping(); }
