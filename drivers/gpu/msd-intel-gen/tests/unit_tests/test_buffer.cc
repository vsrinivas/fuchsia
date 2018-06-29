// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    static void SharedMapping(uint64_t size)
    {
        auto address_space_owner = std::make_unique<AddressSpaceOwner>();
        auto address_space = std::make_shared<MockAddressSpace>(address_space_owner.get(), 0,
                                                                magma::round_up(size, PAGE_SIZE));
        ASSERT_EQ(address_space->type(), ADDRESS_SPACE_PPGTT);

        std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(size, "test"));
        ASSERT_NE(buffer, nullptr);
        EXPECT_EQ(1u, buffer.use_count());

        auto mapping = AddressSpace::GetSharedGpuMapping(address_space, buffer, 0,
                                                         buffer->platform_buffer()->size());
        EXPECT_EQ(2u, buffer.use_count());
        EXPECT_EQ(2u, mapping.use_count());

        auto mapping2 = AddressSpace::GetSharedGpuMapping(address_space, buffer, 0,
                                                          buffer->platform_buffer()->size());
        EXPECT_EQ(mapping, mapping2);
        EXPECT_EQ(2u, buffer.use_count());
        EXPECT_EQ(3u, mapping.use_count());

        mapping.reset();
        mapping2.reset();
        // Mapping retained in the address space
        EXPECT_EQ(2u, buffer.use_count());

        uint32_t release_count;
        address_space->ReleaseBuffer(buffer->platform_buffer(), &release_count);
        EXPECT_EQ(1u, release_count);
        EXPECT_EQ(1u, buffer.use_count());
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
        EXPECT_EQ(1u, buffer.use_count());

        std::shared_ptr<GpuMapping> mapping_low =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2);
        ASSERT_NE(mapping_low, nullptr);
        EXPECT_EQ(2u, buffer.use_count());

        std::shared_ptr<GpuMapping> mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize / 2, kBufferSize / 2);
        ASSERT_NE(mapping_high, nullptr);
        EXPECT_EQ(3u, buffer.use_count());

        // not the same mapping
        EXPECT_NE(mapping_low.get(), mapping_high.get());

        std::shared_ptr<GpuMapping> mapping_full =
            AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize);
        ASSERT_NE(mapping_full, nullptr);
        EXPECT_NE(mapping_full.get(), mapping_low.get());
        EXPECT_NE(mapping_full.get(), mapping_high.get());
        EXPECT_EQ(4u, buffer.use_count());

        mapping_low = AddressSpace::GetSharedGpuMapping(address_space, buffer, 0, kBufferSize / 2);
        ASSERT_NE(mapping_low, nullptr);
        EXPECT_NE(mapping_low.get(), mapping_full.get());

        mapping_high = AddressSpace::GetSharedGpuMapping(
            address_space, buffer, kBufferSize - kBufferSize / 2, kBufferSize / 2);
        ASSERT_NE(mapping_high, nullptr);
        EXPECT_NE(mapping_high.get(), mapping_full.get());
    }
};

TEST(MsdIntelBuffer, CreateAndDestroy) { TestMsdIntelBuffer::CreateAndDestroy(); }

TEST(MsdIntelBuffer, MapGpu) { TestMsdIntelBuffer::MapGpu(); }

TEST(MsdIntelBuffer, SharedMapping)
{
    TestMsdIntelBuffer::SharedMapping(0x400);
    TestMsdIntelBuffer::SharedMapping(0x1000);
    TestMsdIntelBuffer::SharedMapping(0x16000);
}

TEST(MsdIntelBuffer, OverlappedMapping) { TestMsdIntelBuffer::OverlappedMapping(); }
