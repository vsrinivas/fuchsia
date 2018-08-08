// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpu_mapping_cache.h"
#include "mock/mock_address_space.h"
#include "mock/mock_bus_mapper.h"
#include "msd_intel_buffer.h"
#include "gtest/gtest.h"
#include <thread>

class TestMsdIntelBuffer {
public:
    class AddressSpaceOwner : public AddressSpace::Owner {
    public:
        virtual ~AddressSpaceOwner() = default;
        magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    private:
        MockBusMapper bus_mapper_;
    };

    static void CreateAndDestroy()
    {
        std::unique_ptr<MsdIntelBuffer> buffer;
        uint64_t size;

        buffer = MsdIntelBuffer::Create(size = 0, "test");
        EXPECT_EQ(buffer, nullptr);

        buffer = MsdIntelBuffer::Create(size = 100, "test");
        EXPECT_NE(buffer, nullptr);
        EXPECT_GE(buffer->platform_buffer()->size(), size);

        buffer = MsdIntelBuffer::Create(size = 10000, "test");
        EXPECT_NE(buffer, nullptr);
        EXPECT_GE(buffer->platform_buffer()->size(), size);
    }

    static void MapGpu()
    {
        uint64_t base = PAGE_SIZE;
        uint64_t size = PAGE_SIZE * 10;

        auto address_space_owner = std::make_unique<AddressSpaceOwner>();
        auto address_space =
            std::make_shared<MockAddressSpace>(address_space_owner.get(), base, size);

        std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
        ASSERT_NE(buffer, nullptr);

        auto mapping = AddressSpace::MapBufferGpu(address_space, std::move(buffer));
        ASSERT_NE(mapping, nullptr);

        gpu_addr_t gpu_addr = mapping->gpu_addr();

        EXPECT_TRUE(address_space->is_allocated(gpu_addr));
        EXPECT_FALSE(address_space->is_clear(gpu_addr));

        mapping.reset();

        EXPECT_FALSE(address_space->is_allocated(gpu_addr));
        EXPECT_TRUE(address_space->is_clear(gpu_addr));
    }

    static std::shared_ptr<GpuMapping>
    GetSharedGpuMapping(std::shared_ptr<AddressSpace> address_space,
                        std::shared_ptr<MsdIntelBuffer> buffer)
    {
        return AddressSpace::GetSharedGpuMapping(address_space, buffer, 0,
                                                 buffer->platform_buffer()->size());
    }

    static void CachedMapping()
    {
        const uint64_t kBufferSize = 4 * PAGE_SIZE;

        auto address_space_owner = std::make_unique<AddressSpaceOwner>();

        std::shared_ptr<MockAddressSpace> address_space;

        // Verify Uncached Behavior
        address_space = std::make_shared<MockAddressSpace>(address_space_owner.get(), 0,
                                                           kBufferSize * 16, nullptr);
        {
            std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize, "test"));
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            auto shared_mapping = GetSharedGpuMapping(address_space, buffer);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 1u);
            shared_mapping = nullptr;
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            shared_mapping = GetSharedGpuMapping(address_space, buffer);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 1u);
        }

        // Basic Caching of a single buffer
        address_space = std::make_shared<MockAddressSpace>(
            address_space_owner.get(), 0, kBufferSize * 16, GpuMappingCache::Create());
        {
            std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize, "test"));
            EXPECT_EQ(buffer->shared_mapping_count(), 0u);
            auto shared_mapping = GetSharedGpuMapping(address_space, buffer);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 2u);
            shared_mapping = nullptr;
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            shared_mapping = GetSharedGpuMapping(address_space, buffer);
            EXPECT_EQ(buffer->shared_mapping_count(), 1u);
            EXPECT_EQ(shared_mapping.use_count(), 2u);
        }

        // Buffer Eviction
        address_space = std::make_shared<MockAddressSpace>(
            address_space_owner.get(), 0, kBufferSize * 16, GpuMappingCache::Create());
        {
            std::shared_ptr<MsdIntelBuffer> buffer0(MsdIntelBuffer::Create(kBufferSize, "test"));
            std::shared_ptr<MsdIntelBuffer> buffer1(MsdIntelBuffer::Create(kBufferSize, "test"));

            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 0u);

            GetSharedGpuMapping(address_space, buffer0);
            EXPECT_EQ(buffer0->shared_mapping_count(), 1u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 0u);

            GetSharedGpuMapping(address_space, buffer1);
            EXPECT_EQ(buffer0->shared_mapping_count(), 1u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 1u);

            address_space->RemoveCachedMappings(buffer0.get());
            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 1u);

            address_space->RemoveCachedMappings(buffer1.get());
            EXPECT_EQ(buffer0->shared_mapping_count(), 0u);
            EXPECT_EQ(buffer1->shared_mapping_count(), 0u);
        }
    }

    static void SharedMapping(uint64_t size)
    {
        auto address_space_owner = std::make_unique<AddressSpaceOwner>();
        auto address_space = std::make_shared<MockAddressSpace>(address_space_owner.get(), 0,
                                                                magma::round_up(size, PAGE_SIZE));
        ASSERT_EQ(address_space->type(), ADDRESS_SPACE_PPGTT);

        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(size, "test"));
        ASSERT_NE(buffer, nullptr);

        std::unique_ptr<GpuMapping> unique_mapping =
            AddressSpace::MapBufferGpu(address_space, buffer);
        ASSERT_NE(unique_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        std::shared_ptr<GpuMapping> shared_mapping =
            buffer->FindBufferMapping(address_space, 0, size);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->FindBufferMapping(address_space, 0, size);
        EXPECT_EQ(shared_mapping, nullptr);

        shared_mapping = buffer->ShareBufferMapping(std::move(unique_mapping));
        EXPECT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(address_space, 0, size);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        {
            std::shared_ptr<GpuMapping> copy = GetSharedGpuMapping(address_space, buffer);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        shared_mapping.reset();

        EXPECT_EQ(buffer->shared_mapping_count(), 0u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(address_space, 0, size);
            EXPECT_EQ(copy, nullptr);
        }

        shared_mapping = GetSharedGpuMapping(address_space, buffer);
        ASSERT_NE(shared_mapping, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy = buffer->FindBufferMapping(address_space, 0, size);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        {
            std::shared_ptr<GpuMapping> copy = GetSharedGpuMapping(address_space, buffer);
            ASSERT_NE(copy, nullptr);
            EXPECT_EQ(copy.get(), shared_mapping.get());
        }

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);
    }

    static void OverlappedMapping()
    {
        auto address_space_owner = std::make_unique<AddressSpaceOwner>();
        auto address_space =
            std::make_shared<MockAddressSpace>(address_space_owner.get(), 0, PAGE_SIZE * 10);
        ASSERT_EQ(address_space->type(), ADDRESS_SPACE_PPGTT);

        constexpr uint32_t kBufferSize = PAGE_SIZE * 6;
        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize, "test"));
        ASSERT_NE(buffer, nullptr);

        std::shared_ptr<GpuMapping> mapping_low =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2);
        ASSERT_NE(mapping_low, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        std::shared_ptr<GpuMapping> mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize / 2, kBufferSize / 2);
        ASSERT_NE(mapping_high, nullptr);

        EXPECT_EQ(buffer->shared_mapping_count(), 2u);

        // not the same mapping
        EXPECT_NE(mapping_low.get(), mapping_high.get());

        std::shared_ptr<GpuMapping> mapping_full =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize);
        ASSERT_NE(mapping_full, nullptr);
        EXPECT_NE(mapping_full.get(), mapping_low.get());
        EXPECT_NE(mapping_full.get(), mapping_high.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 3u);

        mapping_low.reset();
        mapping_high.reset();

        EXPECT_EQ(buffer->shared_mapping_count(), 1u);

        mapping_low = AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2);
        ASSERT_NE(mapping_low, nullptr);
        EXPECT_NE(mapping_low.get(), mapping_full.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 2u);

        mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize - kBufferSize / 2, kBufferSize / 2);
        ASSERT_NE(mapping_high, nullptr);
        EXPECT_NE(mapping_high.get(), mapping_full.get());

        EXPECT_EQ(buffer->shared_mapping_count(), 3u);
    }
};

TEST(MsdIntelBuffer, CreateAndDestroy) { TestMsdIntelBuffer::CreateAndDestroy(); }

TEST(MsdIntelBuffer, MapGpu) { TestMsdIntelBuffer::MapGpu(); }

TEST(MsdIntelBuffer, SharedMapping)
{
    std::vector<uint64_t> sizes = {0x400, 0x1000, 0x16000};

    for (auto size : sizes) {
        TestMsdIntelBuffer::SharedMapping(size);
    }
}

TEST(MsdIntelBuffer, OverlappedMapping) { TestMsdIntelBuffer::OverlappedMapping(); }

TEST(MsdIntelBuffer, CachedMapping) { TestMsdIntelBuffer::CachedMapping(); }
