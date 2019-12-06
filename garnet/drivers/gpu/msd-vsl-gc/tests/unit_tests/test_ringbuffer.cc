// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/gpu/msd-vsl-gc/src/address_space.h"
#include "garnet/drivers/gpu/msd-vsl-gc/src/ringbuffer.h"
#include "gtest/gtest.h"
#include "mock/mock_bus_mapper.h"

class RingbufferTest : public ::testing::Test {
 public:
  class MockAddressSpaceOwner : public magma::AddressSpaceOwner {
   public:
    // Put bus addresses close to the 40 bit limit
    MockAddressSpaceOwner() : bus_mapper_((1ul << (40 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

   private:
    MockBusMapper bus_mapper_;
  };

  static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }
};

TEST_F(RingbufferTest, Map) {
  const uint32_t kRingbufferSize = magma::page_size();
  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), 0 /* start_offset */);
  EXPECT_NE(ringbuffer, nullptr);

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner);
  ASSERT_NE(nullptr, address_space);

  EXPECT_TRUE(ringbuffer->Map(address_space));
}

TEST_F(RingbufferTest, Overwrite32) {
  const uint32_t kRingbufferSize = magma::page_size();
  // Start near the end of the ringbuffer so we can test wrapping around.
  const uint32_t kStartOffset = magma::page_size() - (3 * sizeof(uint32_t));
  const uint32_t kStartIndex = kStartOffset / sizeof(uint32_t);

  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), kStartOffset);
  EXPECT_NE(ringbuffer, nullptr);

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner);
  ASSERT_NE(nullptr, address_space);

  EXPECT_TRUE(ringbuffer->Map(address_space));

  // Should not be able to overwrite anything if ringbuffer is empty.
  EXPECT_FALSE(ringbuffer->Overwrite32(0 /* dwords_before_tail */, 0));
  EXPECT_FALSE(ringbuffer->Overwrite32(1 /* dwords_before_tail */, 0));

  // Write a few values to the ringbuffer but don't wrap around.
  uint32_t num_values = 2;
  for (unsigned int i = 0; i < num_values; i++) {
    ringbuffer->Write32(0xFFFFFFFF /* value */);
  }
  // Overwrite the values we just wrote with the expected ringbuffer offset.
  EXPECT_TRUE(ringbuffer->Overwrite32(1 /* dwords_before_tail */, kStartIndex + 1 /* value */));
  EXPECT_TRUE(ringbuffer->Overwrite32(2 /* dwords_before_tail */, kStartIndex));
  // Only wrote 2 values, cannot overwrite at index 3.
  EXPECT_FALSE(ringbuffer->Overwrite32(3 /* dwords_before_tail */, 0));

  // Fill the rest of the ringbuffer. The ringbuffer holds 1 less than the ringbuffer size.
  uint32_t size_dwords = kRingbufferSize / sizeof(uint32_t);
  num_values = size_dwords - num_values - 1;
  for (unsigned int i = 0; i < num_values; i++) {
    ringbuffer->Write32(0xFFFFFFFF /* value */);
  }
  EXPECT_EQ(ringbuffer->tail(), kStartOffset - sizeof(uint32_t));

  // Replace the values we just wrote.
  // The first value we wrote is at the last physical index of the ringbuffer.
  EXPECT_TRUE(ringbuffer->Overwrite32(num_values /* dwords_before_tail */,
                                      kStartIndex + 2 /* value */));
  // Start overwriting values starting from the tail.
  for (unsigned int i = 1; i < num_values; i++) {
    uint32_t expected_index = kStartIndex - 1 - i;
    EXPECT_TRUE(ringbuffer->Overwrite32(i /* dwords_before_tail */, expected_index /* value */));
  }

  // Verify all the values in the ringbuffer have been correctly replaced.
  uint32_t* addr = vaddr(ringbuffer.get());
  ASSERT_NE(addr, nullptr);

  for (unsigned int i = 0; i < size_dwords; i++) {
    // The index before the start index won't be written, as the ringbuffer can only store
    // 1 less than the ringbuffer size.
    uint32_t next_index = (i + 1) % size_dwords;
    if (next_index != kStartIndex) {
      EXPECT_EQ(addr[i], i);
    }
  }
}
