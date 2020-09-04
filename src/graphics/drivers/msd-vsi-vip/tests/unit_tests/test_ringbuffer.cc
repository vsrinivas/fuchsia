// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "mock/mock_bus_mapper.h"
#include "src/graphics/drivers/msd-vsi-vip/src/address_space.h"
#include "src/graphics/drivers/msd-vsi-vip/src/msd_vsi_context.h"
#include "src/graphics/drivers/msd-vsi-vip/src/ringbuffer.h"

class RingbufferTest : public ::testing::Test {
 public:
  class MockAddressSpaceOwner : public AddressSpace::Owner {
   public:
    // Put bus addresses close to the 40 bit limit
    MockAddressSpaceOwner() : bus_mapper_((1ul << (40 - 1))) {}

    magma::PlatformBusMapper* GetBusMapper() override { return &bus_mapper_; }

    void AddressSpaceReleased(AddressSpace* address_space) override {}

   private:
    MockBusMapper bus_mapper_;
  };

  static uint32_t* vaddr(Ringbuffer* ringbuffer) { return ringbuffer->vaddr(); }
};

TEST_F(RingbufferTest, Map) {
  const uint32_t kRingbufferSize = magma::page_size();
  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  EXPECT_NE(ringbuffer, nullptr);

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner, 0);
  ASSERT_NE(nullptr, address_space);

  auto context =
      MsdVsiContext::Create(std::weak_ptr<MsdVsiConnection>(), address_space, ringbuffer.get());

  EXPECT_TRUE(context->MapRingbuffer(ringbuffer.get()));
}

TEST_F(RingbufferTest, OffsetPopulatedEmpty) {
  const uint32_t kRingbufferSize = 4096;

  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  ASSERT_NE(ringbuffer, nullptr);

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(0));
  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(4096));
}

TEST_F(RingbufferTest, OffsetPopulatedHeadBeforeTail) {
  const uint32_t kRingbufferSize = 4096;

  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  ASSERT_NE(ringbuffer, nullptr);

  const uint32_t kStartOffset = 40;
  ringbuffer->Reset(kStartOffset);

  ringbuffer->update_tail(100);

  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(40));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(60));
  EXPECT_TRUE(ringbuffer->IsOffsetPopulated(96));

  EXPECT_FALSE(ringbuffer->IsOffsetPopulated(100));
}

TEST_F(RingbufferTest, OffsetPopulatedTailBeforeHead) {
  const uint32_t kRingbufferSize = 4096;

  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  ASSERT_NE(ringbuffer, nullptr);

  const uint32_t kStartOffset = 4000;
  ringbuffer->Reset(kStartOffset);

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

  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  EXPECT_NE(ringbuffer, nullptr);

  EXPECT_TRUE(ringbuffer->MapCpu());

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner, 0);
  ASSERT_NE(nullptr, address_space);
  auto context =
      MsdVsiContext::Create(std::weak_ptr<MsdVsiConnection>(), address_space, ringbuffer.get());
  EXPECT_TRUE(context->MapRingbuffer(ringbuffer.get()));

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

TEST_F(RingbufferTest, UsedSize) {
  const uint32_t kRingbufferSize = magma::page_size();

  auto ringbuffer =
      std::make_unique<Ringbuffer>(MsdVsiBuffer::Create(kRingbufferSize, "ringbuffer"));
  EXPECT_NE(ringbuffer, nullptr);

  EXPECT_TRUE(ringbuffer->MapCpu());

  MockAddressSpaceOwner owner;
  std::shared_ptr<AddressSpace> address_space = AddressSpace::Create(&owner, 0);
  ASSERT_NE(nullptr, address_space);
  auto context =
      MsdVsiContext::Create(std::weak_ptr<MsdVsiConnection>(), address_space, ringbuffer.get());
  EXPECT_TRUE(context->MapRingbuffer(ringbuffer.get()));

  const uint32_t max_capacity = ringbuffer->size() - sizeof(uint32_t);
  // Fill the ringbuffer.
  for (unsigned int i = 0; i < max_capacity; i += sizeof(uint32_t)) {
    EXPECT_EQ(ringbuffer->UsedSize(), i);
    ringbuffer->Write32(0xFFFFFFFF /* value */);
  }
  EXPECT_EQ(ringbuffer->UsedSize(), max_capacity);

  // Update the head and verify the used size is updated.
  constexpr uint32_t new_head = 0x500;
  ringbuffer->update_head(new_head);
  EXPECT_EQ(ringbuffer->UsedSize(), max_capacity - new_head);

  // Fill the ringbuffer again.
  for (unsigned int i = 0; i < new_head; i += sizeof(uint32_t)) {
    EXPECT_EQ(ringbuffer->UsedSize(), max_capacity - new_head + i);
    ringbuffer->Write32(0xFFFFFFFF /* value */);
  }
  EXPECT_EQ(ringbuffer->UsedSize(), max_capacity);
}
