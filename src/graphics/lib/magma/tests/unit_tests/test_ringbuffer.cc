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
  EXPECT_TRUE(ringbuffer->Map(address_space));

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
