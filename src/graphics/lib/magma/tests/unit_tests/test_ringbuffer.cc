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
};

TEST_F(TestRingbuffer, CreateAndDestroy) {
  const uint32_t kMagmaPageSize = magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kMagmaPageSize, "test"));

  EXPECT_EQ(ringbuffer->size(), kMagmaPageSize);
  EXPECT_EQ(ringbuffer->head(), 0u);
  EXPECT_EQ(ringbuffer->tail(), 0u);

  const uint32_t kStartOffset = kMagmaPageSize - 12;
  ringbuffer->Reset(kStartOffset);

  EXPECT_EQ(ringbuffer->head(), kStartOffset);
  EXPECT_EQ(ringbuffer->tail(), kStartOffset);
  EXPECT_EQ(ringbuffer->head(), ringbuffer->tail());
}

TEST_F(TestRingbuffer, Size) {
  const uint32_t kRingbufferSize = magma::page_size();
  const uint32_t kBufferSize = kRingbufferSize + magma::page_size();

  auto ringbuffer = std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kBufferSize, "test"),
                                                 kRingbufferSize);
  EXPECT_EQ(ringbuffer->size(), kRingbufferSize);

  const uint64_t kGpuAddr = 0x10000;
  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space =
      std::make_shared<NonAllocatingAddressSpace>(owner.get(), kGpuAddr + kBufferSize);

  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(ringbuffer->MultiMap(address_space, kGpuAddr, &gpu_mapping));
  EXPECT_EQ(kBufferSize, address_space->inserted_size(kGpuAddr));
  EXPECT_NE(gpu_mapping, nullptr);
}

TEST_F(TestRingbuffer, Write) {
  const uint32_t kMagmaPageSize = magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kMagmaPageSize, "test"));
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
      std::make_unique<Ringbuffer>(magma::PlatformBuffer::Create(kMagmaPageSize, "test"));

  auto owner = std::make_unique<AddressSpaceOwner>();
  auto address_space = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);

  const uint64_t kGpuAddr = 0x10000;
  std::shared_ptr<GpuMapping> gpu_mapping;
  EXPECT_TRUE(ringbuffer->MultiMap(address_space, kGpuAddr, &gpu_mapping));
  EXPECT_NE(gpu_mapping, nullptr);

  // Try mapping additional address spaces.
  auto address_space2 = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  std::shared_ptr<GpuMapping> gpu_mapping2;
  EXPECT_TRUE(ringbuffer->MultiMap(address_space2, kGpuAddr, &gpu_mapping2));
  EXPECT_NE(gpu_mapping2, nullptr);

  auto address_space3 = std::make_shared<NonAllocatingAddressSpace>(owner.get(), UINT32_MAX);
  std::shared_ptr<GpuMapping> gpu_mapping3;
  EXPECT_TRUE(ringbuffer->MultiMap(address_space3, kGpuAddr, &gpu_mapping3));
  EXPECT_NE(gpu_mapping3, nullptr);
}
