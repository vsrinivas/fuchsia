// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <magma_util/address_space.h>
#include <magma_util/gpu_mapping.h>
#include <magma_util/ringbuffer.h>
#include <mock/fake_address_space.h>
#include <mock/mock_bus_mapper.h>

using GpuMapping = magma::GpuMapping<magma::PlatformBuffer>;
using AllocatingAddressSpace =
    FakeAllocatingAddressSpace<GpuMapping, magma::AddressSpace<GpuMapping>>;
using NonAllocatingAddressSpace =
    FakeNonAllocatingAddressSpace<GpuMapping, magma::AddressSpace<GpuMapping>>;
using Ringbuffer = magma::Ringbuffer<GpuMapping>;

class TestRingbuffer : public ::testing::Test {
 public:
  class AddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    virtual ~AddressSpaceOwner() = default;
    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }

  static uint64_t gpu_mapping_count(Ringbuffer* ringbuffer) {
    return ringbuffer->gpu_mappings_.size();
  }
};

TEST_F(TestRingbuffer, CreateAndDestroy) {
  const uint32_t kMagmaPageSize = magma::page_size();
  const uint32_t kStartOffset = kMagmaPageSize - 10;
  auto ringbuffer = std::make_unique<Ringbuffer>(
      magma::PlatformBuffer::Create(kMagmaPageSize, "test"), kStartOffset);
  EXPECT_EQ(ringbuffer->size(), kMagmaPageSize);
  EXPECT_EQ(ringbuffer->head(), kStartOffset);
  EXPECT_EQ(ringbuffer->head(), ringbuffer->tail());
}

TEST_F(TestRingbuffer, Write) {
  const uint32_t kMagmaPageSize = magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kMagmaPageSize, "test"), 0);
  EXPECT_EQ(ringbuffer->size(), kMagmaPageSize);
  EXPECT_EQ(ringbuffer->head(), 0u);

  // Can't store full size because head==tail means empty
  EXPECT_FALSE(ringbuffer->HasSpace(kMagmaPageSize));
  EXPECT_TRUE(ringbuffer->HasSpace(kMagmaPageSize - sizeof(uint32_t)));

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<AllocatingAddressSpace>(owner.get(), 0x10000,  // base
                                                                kMagmaPageSize);
  uint64_t gpu_addr;
  EXPECT_TRUE(ringbuffer->Map(address_space, &gpu_addr));

  uint32_t* addr = vaddr(ringbuffer.get());
  ASSERT_NE(addr, nullptr);

  uint32_t start_index = ringbuffer->tail() / sizeof(uint32_t);
  uint32_t size_dwords = kMagmaPageSize / sizeof(uint32_t);

  // Stuff the ringbuffer - fill to one less
  for (unsigned int i = 0; i < size_dwords - 1; i++) {
    EXPECT_TRUE(ringbuffer->HasSpace(sizeof(uint32_t)));
    ringbuffer->Write32(i);
    EXPECT_EQ(addr[(start_index + i) % size_dwords], i);
  }

  ringbuffer->update_head(ringbuffer->tail());

  // Do it again
  for (unsigned int i = 0; i < size_dwords - 1; i++) {
    EXPECT_TRUE(ringbuffer->HasSpace(sizeof(uint32_t)));
    ringbuffer->Write32(i);
    EXPECT_EQ(addr[(start_index + i) % size_dwords], i);
  }
}

TEST_F(TestRingbuffer, MultipleAddressSpaces) {
  const uint32_t kMagmaPageSize = magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kMagmaPageSize, "test"), 0);

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);

  const uint64_t kGpuAddr = 0x10000;
  EXPECT_TRUE(ringbuffer->MultiMap(address_space, kGpuAddr));

  // Try mapping additional address spaces.
  auto address_space2 = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  EXPECT_TRUE(ringbuffer->MultiMap(address_space2, kGpuAddr));

  auto address_space3 = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  EXPECT_TRUE(ringbuffer->MultiMap(address_space3, kGpuAddr));

  EXPECT_EQ(gpu_mapping_count(ringbuffer.get()), 3uL);

  // Drop two of the address spaces and try mapping the second address space again.
  address_space.reset();
  address_space3.reset();

  EXPECT_FALSE(ringbuffer->MultiMap(address_space2, kGpuAddr));
  EXPECT_EQ(gpu_mapping_count(ringbuffer.get()), 1uL);
}
