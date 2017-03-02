// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping_cache.h"
#include "magma_util/sleep.h"
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

    static void CachedMapping()
    {

        const uint64_t kBufferSize = 4 * PAGE_SIZE;

        std::shared_ptr<MockAddressSpace> address_space;

        // Verify Uncached Behavior
        address_space = std::make_shared<MockAddressSpace>(0, kBufferSize * 16, 0);
        {
            std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize));
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            auto shared_mapping =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, PAGE_SIZE);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 1u);
            shared_mapping = nullptr;
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            shared_mapping = AddressSpace::GetSharedGpuMapping(address_space, buffer, PAGE_SIZE);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 1u);
        }

        // Basic Caching of a single buffer
        address_space = std::make_shared<MockAddressSpace>(0, kBufferSize * 16, kBufferSize);
        {
            std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize));
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            auto shared_mapping =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, PAGE_SIZE);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 2u);
            shared_mapping = nullptr;
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            shared_mapping = AddressSpace::GetSharedGpuMapping(address_space, buffer, PAGE_SIZE);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 2u);
        }

        // Buffer Eviction
        address_space = std::make_shared<MockAddressSpace>(0, kBufferSize * 16, kBufferSize);
        {
            std::shared_ptr<MsdIntelBuffer> buffer0(MsdIntelBuffer::Create(kBufferSize));
            std::shared_ptr<MsdIntelBuffer> buffer1(MsdIntelBuffer::Create(kBufferSize));
            std::shared_ptr<MsdIntelBuffer> buffer2(MsdIntelBuffer::Create(2 * kBufferSize));

            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer2->shared_mapping_count(), 0u);
            AddressSpace::GetSharedGpuMapping(address_space, buffer0, PAGE_SIZE);
            EXPECT_EQ(buffer0->shared_mapping_count(), 1u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer2->shared_mapping_count(), 0u);
            AddressSpace::GetSharedGpuMapping(address_space, buffer1, PAGE_SIZE);
            // buffer1 should fit in the cache and therefore evict buffer0
            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 1u);
            EXPECT_EQ(buffer2->shared_mapping_count(), 0u);

            AddressSpace::GetSharedGpuMapping(address_space, buffer2, PAGE_SIZE);
            // buffer2 should not fit in the cache and therefore not evict buffer1
            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 1u);
            EXPECT_EQ(buffer2->shared_mapping_count(), 0u);
        }
    }

    static void SharedMapping(uint64_t size, uint32_t alignment)
    {
        std::shared_ptr<MockAddressSpace> address_space(
            new MockAddressSpace(0, magma::round_up(size, PAGE_SIZE)));
        ASSERT_EQ(address_space->type(), ADDRESS_SPACE_GGTT);

        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(size));
        ASSERT_NE(buffer, nullptr);

        std::unique_ptr<GpuMapping> unique_mapping =
            AddressSpace::MapBufferGpu(address_space, buffer, alignment);
        ASSERT_NE(unique_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        std::shared_ptr<GpuMapping> shared_mapping =
            buffer->FindBufferMapping(address_space, 0, size, alignment);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->FindBufferMapping(address_space, 0, size, alignment);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->ShareBufferMapping(std::move(unique_mapping));
        EXPECT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy =
                buffer->FindBufferMapping(address_space, 0, size, alignment);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        {
            std::shared_ptr<GpuMapping> copy =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, alignment);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        shared_mapping.reset();

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        {
            std::shared_ptr<GpuMapping> copy =
                buffer->FindBufferMapping(address_space, 0, size, alignment);
            EXPECT_EQ(copy, nullptr);
        }

        shared_mapping = AddressSpace::GetSharedGpuMapping(address_space, buffer, alignment);
        ASSERT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy =
                buffer->FindBufferMapping(address_space, 0, size, alignment);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy =
                AddressSpace::GetSharedGpuMapping(address_space, buffer, alignment);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);
    }

    static void OverlappedMapping(uint32_t alignment)
    {
        std::shared_ptr<MockAddressSpace> address_space(new MockAddressSpace(0, PAGE_SIZE * 10));
        ASSERT_EQ(address_space->type(), ADDRESS_SPACE_GGTT);

        constexpr uint32_t kBufferSize = PAGE_SIZE * 6;
        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize));
        ASSERT_NE(buffer, nullptr);

        std::shared_ptr<GpuMapping> mapping_low =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2, alignment);
        ASSERT_NE(mapping_low, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        std::shared_ptr<GpuMapping> mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize / 2, kBufferSize / 2, alignment);
        ASSERT_NE(mapping_high, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 2u);

        // not the same mapping
        EXPECT_NE(mapping_low.get(), mapping_high.get());

        std::shared_ptr<GpuMapping> mapping_full =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize, alignment);
        ASSERT_NE(mapping_full, nullptr);
        EXPECT_NE(mapping_full.get(), mapping_low.get());
        EXPECT_NE(mapping_full.get(), mapping_high.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 3u);

        mapping_low.reset();
        mapping_high.reset();

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        mapping_low =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2, alignment);
        ASSERT_NE(mapping_low, nullptr);
        EXPECT_NE(mapping_low.get(), mapping_full.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 2u);

        mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize - kBufferSize / 2, kBufferSize / 2, alignment);
        ASSERT_NE(mapping_high, nullptr);
        EXPECT_NE(mapping_high.get(), mapping_full.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 3u);
    }

    static void WaitRendering()
    {
        auto buffer = MsdIntelBuffer::Create(PAGE_SIZE);
        uint32_t val = 0;

        buffer->IncrementInflightCounter();
        buffer->IncrementInflightCounter();

        std::thread wait_thread(
            [](MsdIntelBuffer* buffer, uint32_t* val) {
                buffer->WaitRendering();
                EXPECT_EQ(2u, *val);
            },
            buffer.get(), &val);

        magma::msleep(1000);
        ++val;
        buffer->DecrementInflightCounter();

        magma::msleep(1000);
        ++val;
        buffer->DecrementInflightCounter();

        EXPECT_EQ(0u, buffer->inflight_counter());

        wait_thread.join();
    }
};

TEST(MsdIntelBuffer, CreateAndDestroy) { TestMsdIntelBuffer::CreateAndDestroy(); }

TEST(MsdIntelBuffer, MapGpu)
{
    TestMsdIntelBuffer::MapGpu(0);
    TestMsdIntelBuffer::MapGpu(8);
    TestMsdIntelBuffer::MapGpu(16);
    TestMsdIntelBuffer::MapGpu(64);
    TestMsdIntelBuffer::MapGpu(4096);
    TestMsdIntelBuffer::MapGpu(8192);
}

TEST(MsdIntelBuffer, SharedMapping)
{
    std::vector<uint64_t> sizes = {0x400, 0x1000, 0x16000};
    std::vector<uint32_t> alignments = {0, 8, 16, 64, 4096};

    for (auto size : sizes)
        for (auto alignment : alignments)
            TestMsdIntelBuffer::SharedMapping(size, alignment);
}

TEST(MsdIntelBuffer, OverlappedMapping)
{
    TestMsdIntelBuffer::OverlappedMapping(0);
    TestMsdIntelBuffer::OverlappedMapping(8);
    TestMsdIntelBuffer::OverlappedMapping(16);
    TestMsdIntelBuffer::OverlappedMapping(64);
    TestMsdIntelBuffer::OverlappedMapping(4096);
    TestMsdIntelBuffer::OverlappedMapping(8192);
}

TEST(MsdIntelBuffer, CachedMapping) { TestMsdIntelBuffer::CachedMapping(); }

TEST(MsdIntelBuffer, WaitRendering) { TestMsdIntelBuffer::WaitRendering(); }
