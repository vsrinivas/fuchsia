// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <magma_util/address_space.h>
#include <magma_util/gpu_mapping.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

using GpuMapping = magma::GpuMapping<magma::PlatformBuffer>;
using AllocatingAddressSpace =
    FakeAllocatingAddressSpace<GpuMapping, magma::AddressSpace<GpuMapping>>;
using NonAllocatingAddressSpace =
    FakeNonAllocatingAddressSpace<GpuMapping, magma::AddressSpace<GpuMapping>>;

// Testing for classes AddressSpace and, indirectly, GpuMapping
class TestAddressSpace : public ::testing::Test {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    MockBusMapper bus_mapper_;
  };
};

TEST_F(TestAddressSpace, AddMapping) {
  constexpr uint32_t kPageCount = 5;
  constexpr uint32_t kGpuAddr = 0x1000;    // arbitrary
  constexpr uint32_t kGpuAddr2 = 0x10000;  // non overlapped

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_EQ(2u, buffer.use_count());
  EXPECT_EQ(kGpuAddr, gpu_mapping->gpu_addr());
  EXPECT_EQ(0u, gpu_mapping->offset());
  EXPECT_EQ(kPageCount * magma::page_size(), gpu_mapping->length());

  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr2,   // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_EQ(3u, buffer.use_count());
  EXPECT_EQ(kGpuAddr2, gpu_mapping->gpu_addr());
  EXPECT_EQ(0u, gpu_mapping->offset());
  EXPECT_EQ(kPageCount * magma::page_size(), gpu_mapping->length());

  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));
}

TEST_F(TestAddressSpace, OverlappedMapping) {
  constexpr uint32_t kPageCount = 2;
  constexpr uint32_t kGpuAddr = 0x1000;  // arbitrary

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_FALSE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                       kGpuAddr - magma::page_size(),  // gpu addr
                                                       0,           // page offset
                                                       kPageCount,  // page count
                                                       &gpu_mapping));

  EXPECT_FALSE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                       kGpuAddr + magma::page_size(),  // gpu addr
                                                       0,           // page offset
                                                       kPageCount,  // page count
                                                       &gpu_mapping));
}

TEST_F(TestAddressSpace, AdjacentMappings) {
  constexpr uint32_t kPageCount = 2;
  constexpr uint32_t kGpuAddr = 0x10000;  // arbitrary

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  // Map in the middle
  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  // Adjacent above
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr + (kPageCount * magma::page_size()),  // gpu addr
      0,                                             // page offset
      kPageCount,                                    // page count
      &gpu_mapping));

  // Adjacent below
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr - (kPageCount * magma::page_size()),  // gpu addr
      0,                                             // page offset
      kPageCount,                                    // page count
      &gpu_mapping));
}

TEST_F(TestAddressSpace, ReleaseMapping) {
  constexpr uint32_t kPageCount = 1;
  constexpr uint32_t kGpuAddr = 0x1000;  // arbitrary

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_FALSE(address_space->ReleaseMapping(buffer.get(), kGpuAddr, &gpu_mapping));

  EXPECT_FALSE(address_space->FindGpuMapping(kGpuAddr));

  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_EQ(2u, buffer.use_count());
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(address_space->FindGpuMapping(kGpuAddr));

  EXPECT_TRUE(address_space->ReleaseMapping(buffer.get(), kGpuAddr, &gpu_mapping));
  gpu_mapping.reset();
  EXPECT_EQ(1u, buffer.use_count());

  EXPECT_FALSE(address_space->FindGpuMapping(kGpuAddr));

  // Validate we can re-map
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_EQ(2u, buffer.use_count());

  EXPECT_FALSE(address_space->FindGpuMapping(kGpuAddr));
}

TEST_F(TestAddressSpace, ReleaseBuffer) {
  constexpr uint32_t kPageCount = 1;
  constexpr uint32_t kGpuAddr = 0x1000;  // arbitrary

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_EQ(2u, buffer.use_count());
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr + kPageCount * magma::page_size(),  // gpu addr
      0,                                           // page offset
      kPageCount,                                  // page count
      &gpu_mapping));
  EXPECT_EQ(3u, buffer.use_count());
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(
      NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                              kGpuAddr + 10 * magma::page_size(),  // gpu addr
                                              0,                                   // page offset
                                              kPageCount,                          // page count
                                              &gpu_mapping));
  EXPECT_EQ(4u, buffer.use_count());
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  std::vector<std::shared_ptr<GpuMapping>> released_mappings;
  address_space->ReleaseBuffer(buffer.get(), &released_mappings);

  EXPECT_EQ(3u, released_mappings.size());
  released_mappings.clear();
  gpu_mapping.reset();
  EXPECT_EQ(1u, buffer.use_count());

  // Validate we can re-use the same addresses
  buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,    // gpu addr
                                                      0,           // page offset
                                                      kPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr + kPageCount * magma::page_size(),  // gpu addr
      0,                                           // page offset
      kPageCount,                                  // page count
      &gpu_mapping));
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(
      NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                              kGpuAddr + 10 * magma::page_size(),  // gpu addr
                                              0,                                   // page offset
                                              kPageCount,                          // page count
                                              &gpu_mapping));
}

TEST_F(TestAddressSpace, AllocatingMap) {
  constexpr uint32_t kPageCount = 1;
  constexpr uint32_t kStartAddr = 0x1000;

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space =
      std::make_shared<AllocatingAddressSpace>(owner.get(), kStartAddr, UINT32_MAX - kStartAddr);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping =
      AllocatingAddressSpace::MapBufferGpu(address_space, buffer);
  EXPECT_TRUE(gpu_mapping);
  EXPECT_EQ(kStartAddr, gpu_mapping->gpu_addr());
  EXPECT_EQ(2u, buffer.use_count());
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));
}

TEST_F(TestAddressSpace, FindMapping) {
  constexpr uint32_t kPageCount = 5;
  constexpr uint32_t kPageOffset = 1;
  constexpr uint32_t kMappingPageCount = kPageCount - kPageOffset;
  constexpr uint32_t kGpuAddr = 0x1000;  // arbitrary

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kPageCount * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,           // gpu addr
                                                      kPageOffset,        // page offset
                                                      kMappingPageCount,  // page count
                                                      &gpu_mapping));
  EXPECT_TRUE(address_space->AddMapping(gpu_mapping));

  EXPECT_TRUE(address_space->FindGpuMapping(kGpuAddr));
  EXPECT_TRUE(address_space->FindGpuMapping(buffer.get(), kPageOffset * magma::page_size(),
                                            kMappingPageCount * magma::page_size()));
  EXPECT_TRUE(address_space->FindGpuMapping(buffer.get(), kPageOffset * magma::page_size(),
                                            (kMappingPageCount - 1) * magma::page_size()));

  // Incorrect page offset
  EXPECT_FALSE(
      address_space->FindGpuMapping(buffer.get(), 0, kMappingPageCount * magma::page_size()));

  // Incorrect page count
  EXPECT_FALSE(address_space->FindGpuMapping(buffer.get(), kPageOffset * magma::page_size(),
                                             (kMappingPageCount + 1) * magma::page_size()));
}

TEST_F(TestAddressSpace, GrowMapping) {
  constexpr uint32_t kGpuAddr = 0x1000;
  constexpr uint32_t kSpaceSizeInPages = 10;
  constexpr uint32_t kBufferSizeInPages = 8;
  constexpr uint32_t kBufferPagesToGrow = 1;

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(
      owner.get(), magma::page_size() * kSpaceSizeInPages);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kBufferSizeInPages * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr,                                 // gpu addr
      0,                                        // page offset
      kBufferSizeInPages - kBufferPagesToGrow,  // page count
      &mapping));
  ASSERT_TRUE(mapping);

  uint64_t orig_length = (kBufferSizeInPages - kBufferPagesToGrow) * magma::page_size();
  EXPECT_EQ(mapping->length(), orig_length);

  EXPECT_TRUE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
  EXPECT_EQ(mapping->length(), kBufferSizeInPages * magma::page_size());

  // Can't map on top of grown area
  std::shared_ptr<GpuMapping> mapping2;
  EXPECT_FALSE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                       kGpuAddr + orig_length,  // gpu addr
                                                       0,                       // page offset
                                                       kBufferSizeInPages,      // page count
                                                       &mapping2));

  std::vector<std::unique_ptr<magma::PlatformBusMapper::BusMapping>> mappings;
  EXPECT_TRUE(mapping->Release(&mappings));
  EXPECT_EQ(2u, mappings.size());
}

TEST_F(TestAddressSpace, GrowMappingErrorOutsideBuffer) {
  constexpr uint32_t kGpuAddr = 0x1000;
  constexpr uint32_t kSpaceSizeInPages = 10;
  constexpr uint32_t kBufferSizeInPages = 8;
  constexpr uint32_t kBufferPagesToGrow = 1;

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(
      owner.get(), magma::page_size() * kSpaceSizeInPages);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kBufferSizeInPages * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,            // gpu addr
                                                      0,                   // page offset
                                                      kBufferSizeInPages,  // page count
                                                      &mapping));
  ASSERT_TRUE(mapping);

  EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
}

TEST_F(TestAddressSpace, GrowMappingErrorOutsideSpace) {
  constexpr uint32_t kGpuAddr = 0;
  constexpr uint32_t kSpaceSizeInPages = 10;
  constexpr uint32_t kBufferSizeInPages = 12;
  constexpr uint32_t kBufferPagesToGrow = 1;

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(
      owner.get(), magma::page_size() * kSpaceSizeInPages);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kBufferSizeInPages * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,           // gpu addr
                                                      0,                  // page offset
                                                      kSpaceSizeInPages,  // page count
                                                      &mapping));
  ASSERT_TRUE(mapping);

  EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
}

TEST_F(TestAddressSpace, GrowMappingErrorOverlapped) {
  constexpr uint32_t kGpuAddr = 0x1000;  // arbitrary
  constexpr uint32_t kSpaceSizeInPages = 10;
  constexpr uint32_t kBufferSizeInPages = 4;
  constexpr uint32_t kBufferPagesToGrow = 1;

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(
      owner.get(), magma::page_size() * kSpaceSizeInPages);
  auto buffer = std::shared_ptr<magma::PlatformBuffer>(
      magma::PlatformBuffer::Create(kBufferSizeInPages * magma::page_size(), "Test"));

  std::shared_ptr<GpuMapping> mapping;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(address_space, buffer,
                                                      kGpuAddr,            // gpu addr
                                                      0,                   // page offset
                                                      kBufferSizeInPages,  // page count
                                                      &mapping));
  ASSERT_TRUE(mapping);

  std::shared_ptr<GpuMapping> mapping2;
  EXPECT_TRUE(NonAllocatingAddressSpace::MapBufferGpu(
      address_space, buffer,
      kGpuAddr + kBufferSizeInPages * magma::page_size(),  // gpu addr
      0,                                                   // page offset
      kBufferSizeInPages,                                  // page count
      &mapping2));
  ASSERT_TRUE(mapping2);

  EXPECT_FALSE(address_space->GrowMapping(mapping.get(), kBufferPagesToGrow));
}
