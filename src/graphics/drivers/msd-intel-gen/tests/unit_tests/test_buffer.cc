// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include <gtest/gtest.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

#include "address_space.h"
#include "msd_intel_buffer.h"

using AllocatingAddressSpace = FakeAllocatingAddressSpace<GpuMapping, AddressSpace>;
using NonAllocatingAddressSpace = FakeNonAllocatingAddressSpace<GpuMapping, AddressSpace>;

class TestMsdIntelBuffer {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  static void CreateAndDestroy() {
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

  static void AllocatingMapGpu() {
    uint64_t base = PAGE_SIZE;
    uint64_t size = PAGE_SIZE * 10;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space =
        std::make_shared<AllocatingAddressSpace>(address_space_owner.get(), base, size);

    std::unique_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(PAGE_SIZE, "test"));
    ASSERT_NE(buffer, nullptr);

    auto mapping = address_space->MapBufferGpu(address_space, std::move(buffer));
    ASSERT_NE(mapping, nullptr);

    gpu_addr_t gpu_addr = mapping->gpu_addr();

    EXPECT_TRUE(address_space->is_allocated(gpu_addr));
    EXPECT_FALSE(address_space->is_clear(gpu_addr));

    mapping.reset();

    EXPECT_FALSE(address_space->is_allocated(gpu_addr));
    EXPECT_TRUE(address_space->is_clear(gpu_addr));
  }

  static void NonAllocatingMapGpuFail() {
    constexpr uint64_t kAddressSpaceSize = PAGE_SIZE * 10;
    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space =
        std::make_shared<NonAllocatingAddressSpace>(address_space_owner.get(), kAddressSpaceSize);

    constexpr uint64_t kBufferSizeInPages = 2;
    auto buffer = std::shared_ptr<MsdIntelBuffer>(
        MsdIntelBuffer::Create(kBufferSizeInPages * PAGE_SIZE, "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    // Gpu address misaligned
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             PAGE_SIZE + 1,           // gpu addr
                                             0,                       // page offset
                                             kBufferSizeInPages - 1,  // page count
                                             &mapping));
    // Bad page offset
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             PAGE_SIZE,           // gpu addr
                                             kBufferSizeInPages,  // page offset
                                             1,                   // page count
                                             &mapping));
    // Bad page count
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             PAGE_SIZE,               // gpu addr
                                             0,                       // page offset
                                             kBufferSizeInPages + 1,  // page count
                                             &mapping));
    // Bad page offset + count
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             PAGE_SIZE,           // gpu addr
                                             1,                   // page offset
                                             kBufferSizeInPages,  // page count
                                             &mapping));
    // Won't fit
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             kAddressSpaceSize - PAGE_SIZE,  // gpu addr
                                             0,                              // page offset
                                             kBufferSizeInPages,             // page count
                                             &mapping));
  }

  static void NonAllocatingMapGpu() {
    constexpr uint64_t kAddressSpaceSize = PAGE_SIZE * 10;
    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space =
        std::make_shared<NonAllocatingAddressSpace>(address_space_owner.get(), kAddressSpaceSize);

    constexpr uint64_t kBufferSizeInPages = 2;
    auto buffer = std::shared_ptr<MsdIntelBuffer>(
        MsdIntelBuffer::Create(kBufferSizeInPages * PAGE_SIZE, "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    // Start
    ASSERT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            0,                   // gpu addr
                                            0,                   // page offset
                                            kBufferSizeInPages,  // page count
                                            &mapping));
    EXPECT_TRUE(address_space->AddMapping(std::move(mapping)));

    // End
    ASSERT_TRUE(
        address_space->MapBufferGpu(address_space, buffer,
                                    kAddressSpaceSize - kBufferSizeInPages * PAGE_SIZE,  // gpu addr
                                    0,                   // page offset
                                    kBufferSizeInPages,  // page count
                                    &mapping));
    EXPECT_TRUE(address_space->AddMapping(std::move(mapping)));

    // Middle
    ASSERT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            PAGE_SIZE * 5,       // gpu addr
                                            0,                   // page offset
                                            kBufferSizeInPages,  // page count
                                            &mapping));
    EXPECT_TRUE(address_space->AddMapping(std::move(mapping)));

    // Partial from start
    ASSERT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            kBufferSizeInPages * PAGE_SIZE,  // gpu addr
                                            1,                               // page offset
                                            kBufferSizeInPages - 1,          // page count
                                            &mapping));
    EXPECT_TRUE(address_space->AddMapping(std::move(mapping)));

    // Partial from end
    ASSERT_TRUE(address_space->MapBufferGpu(
        address_space, buffer,
        kAddressSpaceSize - (kBufferSizeInPages + 1) * PAGE_SIZE,  // gpu addr
        kBufferSizeInPages - 1,                                    // page offset
        1,                                                         // page count
        &mapping));
    EXPECT_TRUE(address_space->AddMapping(std::move(mapping)));
  }

  static void SharedMapping(uint64_t size) {
    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<AllocatingAddressSpace>(address_space_owner.get(), 0,
                                                                  magma::round_up(size, PAGE_SIZE));

    std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(size, "test"));
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(1u, buffer.use_count());

    std::shared_ptr<GpuMapping> mapping = address_space->FindGpuMapping(
        buffer->platform_buffer(), 0, buffer->platform_buffer()->size());
    EXPECT_FALSE(mapping);

    mapping = address_space->MapBufferGpu(address_space, buffer);
    ASSERT_TRUE(mapping);

    EXPECT_EQ(1u, mapping.use_count());
    EXPECT_EQ(2u, buffer.use_count());

    EXPECT_TRUE(address_space->AddMapping(mapping));
    EXPECT_EQ(2u, mapping.use_count());

    auto mapping2 = address_space->FindGpuMapping(buffer->platform_buffer(), 0,
                                                  buffer->platform_buffer()->size());
    EXPECT_EQ(mapping, mapping2);
    EXPECT_EQ(2u, buffer.use_count());
    EXPECT_EQ(3u, mapping.use_count());

    mapping.reset();
    mapping2.reset();
    // Mapping retained in the address space
    EXPECT_EQ(2u, buffer.use_count());

    std::vector<std::shared_ptr<GpuMapping>> mappings;
    address_space->ReleaseBuffer(buffer->platform_buffer(), &mappings);
    EXPECT_EQ(1u, mappings.size());
    EXPECT_EQ(2u, buffer.use_count());
    mappings.clear();
    EXPECT_EQ(1u, buffer.use_count());
  }

  static void OverlappedMapping() {
    constexpr uint32_t kBufferSize = PAGE_SIZE * 6;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space =
        std::make_shared<AllocatingAddressSpace>(address_space_owner.get(), 0, kBufferSize * 2);

    std::shared_ptr<MsdIntelBuffer> buffer(MsdIntelBuffer::Create(kBufferSize, "test"));
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(1u, buffer.use_count());

    std::shared_ptr<GpuMapping> mapping_low =
        address_space->MapBufferGpu(address_space, buffer, 0, kBufferSize / 2);
    ASSERT_TRUE(mapping_low);
    EXPECT_TRUE(address_space->AddMapping(mapping_low));
    EXPECT_EQ(2u, buffer.use_count());

    std::shared_ptr<GpuMapping> mapping_high =
        address_space->MapBufferGpu(address_space, buffer, kBufferSize / 2, kBufferSize / 2);
    ASSERT_TRUE(mapping_high);
    EXPECT_TRUE(address_space->AddMapping(mapping_high));
    EXPECT_EQ(3u, buffer.use_count());

    // not the same mapping
    EXPECT_NE(mapping_low.get(), mapping_high.get());

    std::shared_ptr<GpuMapping> mapping_full =
        address_space->MapBufferGpu(address_space, buffer, 0, kBufferSize);
    ASSERT_TRUE(mapping_full);

    EXPECT_NE(mapping_full.get(), mapping_low.get());
    EXPECT_NE(mapping_full.get(), mapping_high.get());
    EXPECT_EQ(4u, buffer.use_count());

    auto found_mapping_low =
        address_space->FindGpuMapping(buffer->platform_buffer(), 0, kBufferSize / 2);
    ASSERT_TRUE(found_mapping_low);
    EXPECT_EQ(found_mapping_low.get(), mapping_low.get());

    auto found_mapping_high = address_space->FindGpuMapping(
        buffer->platform_buffer(), kBufferSize - kBufferSize / 2, kBufferSize / 2);
    ASSERT_TRUE(found_mapping_high);
    EXPECT_EQ(found_mapping_high.get(), mapping_high.get());
  }

  static void GrowMapping() {
    constexpr uint32_t kSpaceSizeInPages = 10;
    constexpr uint32_t kBufferSizeInPages = 8;
    constexpr uint32_t kBufferPagesToGrow = 1;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<NonAllocatingAddressSpace>(
        address_space_owner.get(), magma::page_size() * kSpaceSizeInPages);

    std::shared_ptr<MsdIntelBuffer> buffer(
        MsdIntelBuffer::Create(kBufferSizeInPages * magma::page_size(), "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            0,  // gpu addr
                                            0,  // page offset
                                            kBufferSizeInPages - kBufferPagesToGrow, &mapping));
    ASSERT_TRUE(mapping);

    uint64_t orig_length = (kBufferSizeInPages - kBufferPagesToGrow) * magma::page_size();
    EXPECT_EQ(mapping->length(), orig_length);

    EXPECT_TRUE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
    EXPECT_EQ(mapping->length(), kBufferSizeInPages * magma::page_size());

    // Can't map on top of grown area
    std::shared_ptr<GpuMapping> mapping2;
    EXPECT_FALSE(address_space->MapBufferGpu(address_space, buffer,
                                             orig_length,  // gpu addr
                                             0,            // page offset
                                             kBufferSizeInPages, &mapping2));
  }

  static void GrowMappingErrorOutsideBuffer() {
    constexpr uint32_t kSpaceSizeInPages = 10;
    constexpr uint32_t kBufferSizeInPages = 8;
    constexpr uint32_t kBufferPagesToGrow = 1;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<NonAllocatingAddressSpace>(
        address_space_owner.get(), magma::page_size() * kSpaceSizeInPages);

    std::shared_ptr<MsdIntelBuffer> buffer(
        MsdIntelBuffer::Create(kBufferSizeInPages * magma::page_size(), "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            0,  // gpu addr
                                            0,  // page offset
                                            kBufferSizeInPages, &mapping));
    ASSERT_TRUE(mapping);

    EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
  }

  static void GrowMappingErrorOutsideSpace() {
    constexpr uint32_t kSpaceSizeInPages = 10;
    constexpr uint32_t kBufferSizeInPages = 12;
    constexpr uint32_t kBufferPagesToGrow = 1;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<NonAllocatingAddressSpace>(
        address_space_owner.get(), magma::page_size() * kSpaceSizeInPages);

    std::shared_ptr<MsdIntelBuffer> buffer(
        MsdIntelBuffer::Create(kBufferSizeInPages * magma::page_size(), "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            0,  // gpu addr
                                            0,  // page offset
                                            kSpaceSizeInPages, &mapping));
    ASSERT_TRUE(mapping);

    EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
  }

  static void GrowMappingErrorOverlapped() {
    constexpr uint32_t kSpaceSizeInPages = 10;
    constexpr uint32_t kBufferSizeInPages = 4;
    constexpr uint32_t kBufferPagesToGrow = 1;

    auto address_space_owner = std::make_unique<AddressSpaceOwner>();
    auto address_space = std::make_shared<NonAllocatingAddressSpace>(
        address_space_owner.get(), magma::page_size() * kSpaceSizeInPages);

    std::shared_ptr<MsdIntelBuffer> buffer(
        MsdIntelBuffer::Create(kBufferSizeInPages * magma::page_size(), "test"));
    ASSERT_TRUE(buffer);

    std::shared_ptr<GpuMapping> mapping;
    EXPECT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            0,  // gpu addr
                                            0,  // page offset
                                            kBufferSizeInPages, &mapping));
    ASSERT_TRUE(mapping);

    std::shared_ptr<GpuMapping> mapping2;
    EXPECT_TRUE(address_space->MapBufferGpu(address_space, buffer,
                                            kBufferSizeInPages * magma::page_size(),  // gpu addr
                                            0,                                        // page offset
                                            kBufferSizeInPages, &mapping2));
    ASSERT_TRUE(mapping2);

    EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
  }
};

TEST(MsdIntelBuffer, CreateAndDestroy) { TestMsdIntelBuffer::CreateAndDestroy(); }

TEST(MsdIntelBuffer, AllocatingMapGpu) { TestMsdIntelBuffer::AllocatingMapGpu(); }

TEST(MsdIntelBuffer, NonAllocatingMapGpuFail) { TestMsdIntelBuffer::NonAllocatingMapGpuFail(); }

TEST(MsdIntelBuffer, NonAllocatingMapGpu) { TestMsdIntelBuffer::NonAllocatingMapGpu(); }

TEST(MsdIntelBuffer, SharedMapping) {
  TestMsdIntelBuffer::SharedMapping(0x400);
  TestMsdIntelBuffer::SharedMapping(0x1000);
  TestMsdIntelBuffer::SharedMapping(0x16000);
}

TEST(MsdIntelBuffer, OverlappedMapping) { TestMsdIntelBuffer::OverlappedMapping(); }

TEST(MsdIntelBuffer, GrowMapping) { TestMsdIntelBuffer::GrowMapping(); }

TEST(MsdIntelBuffer, GrowMappingErrorOutsideBuffer) {
  TestMsdIntelBuffer::GrowMappingErrorOutsideBuffer();
}

TEST(MsdIntelBuffer, GrowMappingErrorOutsideSpace) {
  TestMsdIntelBuffer::GrowMappingErrorOutsideSpace();
}

TEST(MsdIntelBuffer, GrowMappingErrorOverlapped) {
  TestMsdIntelBuffer::GrowMappingErrorOverlapped();
}
