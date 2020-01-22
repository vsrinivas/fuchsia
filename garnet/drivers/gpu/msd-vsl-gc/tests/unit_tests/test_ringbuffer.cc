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

TEST_F(RingbufferTest, OffsetPopulatedEmpty) {
  const uint32_t kRingbufferSize = 4096;
  const uint32_t kStartOffset = 0;

  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), kStartOffset);
  ASSERT_NE(ringbuffer, nullptr);

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(0));
  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(4096));
}

TEST_F(RingbufferTest, OffsetPopulatedHeadBeforeTail) {
  const uint32_t kRingbufferSize = 4096;
  const uint32_t kStartOffset = 40;

  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), kStartOffset);
  ASSERT_NE(ringbuffer, nullptr);

  ringbuffer->update_tail(100);

  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(40));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(60));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(96));

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(100));
}

TEST_F(RingbufferTest, OffsetPopulatedTailBeforeHead) {
  const uint32_t kRingbufferSize = 4096;
  const uint32_t kStartOffset = 4000;

  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), kStartOffset);
  ASSERT_NE(ringbuffer, nullptr);

  ringbuffer->update_tail(100);

  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(4000));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(4092));

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(4096));

  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(0));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(96));

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(100));
}

TEST_F(RingbufferTest, ReserveContiguous) {
  const uint32_t kRingbufferSize = magma::page_size();
  const uint32_t kStartOffset = 0;

  auto ringbuffer = std::make_unique<Ringbuffer>(
      MsdVslBuffer::Create(kRingbufferSize, "ringbuffer"), kStartOffset);
  EXPECT_NE(ringbuffer, nullptr);

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner);
  ASSERT_NE(nullptr, address_space);
  EXPECT_TRUE(ringbuffer->Map(address_space));

  // Cannot request the same number of bytes as the ringbuffer size,
  // as the ringbuffer holds 4 bytes less.
  EXPECT_FALSE(ringbuffer->ReserveContiguous(kRingbufferSize));
  // Request all the space available.
  EXPECT_TRUE(
      ringbuffer->ReserveContiguous(kRingbufferSize - sizeof(uint32_t) /* reserve_bytes */));
  EXPECT_EQ(ringbuffer->tail(), 0u);  // Tail should stay the same until we write something.

  // Partially fill the ringbuffer, leaving |available_bytes| free.
  const uint32_t available_bytes = 5 * sizeof(uint32_t);
  const uint32_t bytes_written = kRingbufferSize - available_bytes - sizeof(uint32_t);
  for (unsigned int i = 0; i < bytes_written / sizeof(uint32_t); i++) {
    ringbuffer->Write32(0xFFFFFFFF /* value */);
  }
  EXPECT_EQ(ringbuffer->tail(), bytes_written);

  // Ringbuffer state (# = occupied, x = unusable)
  //
  // Contents:  | ####################################### |               |x|
  // Offset:    HEAD (0)                                  TAIL (4072)       END

  // Request slightly more space than is available.
  EXPECT_FALSE(ringbuffer->ReserveContiguous(available_bytes + sizeof(uint32_t)));
  // Request all the space available.
  EXPECT_TRUE(ringbuffer->ReserveContiguous(available_bytes));
  EXPECT_EQ(ringbuffer->tail(), bytes_written);

  // Free up some space in the ringbuffer.
  const uint32_t head_offset = 40;
  ringbuffer->update_head(head_offset);

  // Ringbuffer state
  //
  // Contents:  |           |x| ######################### |               |
  // Offset:    START         HEAD (40)                   TAIL (4072)     END

  // As the head is no longer at 0, we can write an additional 4 bytes contiguously.
  EXPECT_TRUE(ringbuffer->ReserveContiguous(available_bytes + sizeof(uint32_t)));
  EXPECT_EQ(ringbuffer->tail(), bytes_written);

  // There are enough bytes, but not contiguously.
  EXPECT_FALSE(ringbuffer->ReserveContiguous(head_offset));

  // This will reset the tail to get enough contiguous bytes.
  EXPECT_TRUE(ringbuffer->ReserveContiguous(head_offset - sizeof(uint32_t)));
  EXPECT_EQ(ringbuffer->tail(), 0u);
}
