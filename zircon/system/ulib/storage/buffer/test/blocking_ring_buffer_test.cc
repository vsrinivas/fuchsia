// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/blocking_ring_buffer.h"

#include <sched.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>
#include <storage/operation/unbuffered_operations_builder.h>

namespace storage {
namespace {

const uint32_t kBlockSize = 8192;

class MockVmoidRegistry : public VmoidRegistry {
 public:
  vmoid_t default_vmoid() const { return 1; }

 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) override {
    *out = Vmoid(default_vmoid());
    return ZX_OK;
  }
  zx_status_t BlockDetachVmo(Vmoid vmoid) override {
    EXPECT_EQ(default_vmoid(), vmoid.TakeId());
    return ZX_OK;
  }
};

TEST(BlockingRingBufferEmptyTest, EmptyBuffer) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<BlockingRingBuffer> buffer;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            BlockingRingBuffer::Create(&vmoid_registry, 0, kBlockSize, "test-buffer", &buffer));
}

TEST(BlockingRingBufferEmptyTest, EmptyReservation) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(reservation.length(), 0ul);
  EXPECT_EQ(reservation.start(), 0ul);
}

// The arbitrarily-chosen size of the BlockingRingBuffer to use under test (in blocks).
constexpr size_t kBlocks = 5;

class BlockingRingBufferFixture : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(
        BlockingRingBuffer::Create(&vmoid_registry_, kBlocks, kBlockSize, "test-buffer", &buffer_),
        ZX_OK);
  }

  BlockingRingBuffer* buffer() { return buffer_.get(); }
  MockVmoidRegistry& registry() { return vmoid_registry_; }

 private:
  MockVmoidRegistry vmoid_registry_;
  std::unique_ptr<BlockingRingBuffer> buffer_;
};

using BlockingRingBufferTest = BlockingRingBufferFixture;

TEST_F(BlockingRingBufferTest, CapacityTest) { EXPECT_EQ(kBlocks, buffer()->capacity()); }

TEST_F(BlockingRingBufferTest, ReserveOne) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(buffer()->Reserve(1, &reservation), ZX_OK);
  EXPECT_EQ(reservation.start(), 0ul);
  EXPECT_EQ(reservation.length(), 1ul);
}

TEST_F(BlockingRingBufferTest, ReservationMoveConstruction) {
  BlockingRingBufferReservation reservation_a;
  EXPECT_EQ(buffer()->Reserve(1, &reservation_a), ZX_OK);

  BlockingRingBufferReservation reservation_b(std::move(reservation_a));
  EXPECT_EQ(reservation_a.length(), 0ul);
  EXPECT_EQ(reservation_b.length(), 1ul);
}

TEST_F(BlockingRingBufferTest, ReservationMoveAssignment) {
  BlockingRingBufferReservation reservation_a;
  EXPECT_EQ(buffer()->Reserve(1, &reservation_a), ZX_OK);

  BlockingRingBufferReservation reservation_b;
  reservation_b = std::move(reservation_a);
  EXPECT_EQ(reservation_a.length(), 0ul);
  EXPECT_EQ(reservation_b.length(), 1ul);
}

TEST_F(BlockingRingBufferTest, ReservationAtCapacity) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(buffer()->Reserve(kBlocks, &reservation), ZX_OK);
  EXPECT_EQ(kBlocks, reservation.length());
}

// Reserving beyond the capacity of the buffer will always return |ZX_ERR_NO_SPACE|.
TEST_F(BlockingRingBufferTest, ReservationBeyondCapacity) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(ZX_ERR_NO_SPACE, buffer()->Reserve(kBlocks + 1, &reservation));
}

// Reserving beyond the capacity of the buffer will always return |ZX_ERR_NO_SPACE|,
// even when someone else is holding a reservation.
TEST_F(BlockingRingBufferTest, ReservationBeyondCapacityDoesNotBlockWithPriorReservation) {
  BlockingRingBufferReservation reservation_a;
  EXPECT_EQ(buffer()->Reserve(kBlocks, &reservation_a), ZX_OK);
  BlockingRingBufferReservation reservation_b;
  EXPECT_EQ(ZX_ERR_NO_SPACE, buffer()->Reserve(kBlocks + 1, &reservation_b));
}

TEST_F(BlockingRingBufferTest, SingleBlockingReservation) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(buffer()->Reserve(kBlocks, &reservation), ZX_OK);

  // Try acquiring a reservation, but in a background thread, since this call will block.
  std::atomic<bool> made_reservation = false;
  BlockingRingBufferReservation blocking_reservation;
  zx_status_t status = ZX_ERR_BAD_STATE;
  std::thread worker([&] {
    status = buffer()->Reserve(kBlocks, &blocking_reservation);
    made_reservation.store(true);
  });

  sched_yield();
  EXPECT_FALSE(made_reservation.load());
  { auto unused = std::move(reservation); }
  worker.join();
  EXPECT_EQ(status, ZX_OK) << "Reserving buffer in background thread failed";
  EXPECT_TRUE(made_reservation.load());
  EXPECT_EQ(kBlocks, blocking_reservation.length());
}

TEST_F(BlockingRingBufferTest, MultipleBlockingReservations) {
  BlockingRingBufferReservation reservation;
  EXPECT_EQ(buffer()->Reserve(kBlocks, &reservation), ZX_OK);

  // Try acquiring a reservation in multiple blocking background threads.
  std::atomic<bool> made_reservation[kBlocks] = {false};
  std::atomic<zx_status_t> reserve_results[kBlocks] = {ZX_ERR_INTERNAL};
  BlockingRingBufferReservation blocking_reservations[kBlocks];
  std::thread workers[kBlocks];
  for (size_t i = 0; i < kBlocks; i++) {
    workers[i] = std::thread(
        [&](size_t i) {
          reserve_results[i].store(buffer()->Reserve(1, &blocking_reservations[i]));
          made_reservation[i].store(true);
        },
        i);
  }

  for (size_t i = 0; i < kBlocks; i++) {
    EXPECT_FALSE(made_reservation[i].load());
  }

  { auto unused = std::move(reservation); }

  for (size_t i = 0; i < kBlocks; i++) {
    workers[i].join();
    EXPECT_TRUE(made_reservation[i].load());
    EXPECT_EQ(reserve_results[i].load(), ZX_OK);
    EXPECT_EQ(blocking_reservations[i].length(), 1ul);
  }
}

TEST_F(BlockingRingBufferTest, MovingWhileBlockingReservation) {
  BlockingRingBufferReservation reservation_a;
  EXPECT_EQ(buffer()->Reserve(kBlocks, &reservation_a), ZX_OK);

  // Try acquiring a reservation, but in a background thread, since this call will block.
  std::atomic<bool> made_reservation = false;
  std::atomic<zx_status_t> reserve_result = ZX_ERR_INTERNAL;
  BlockingRingBufferReservation blocking_reservation;
  std::thread worker([&] {
    reserve_result.store(buffer()->Reserve(kBlocks, &blocking_reservation));
    made_reservation.store(true);
  });

  EXPECT_FALSE(made_reservation.load());

  // Moving constructing and destruction does not release the reservation.
  BlockingRingBufferReservation reservation_b(std::move(reservation_a));
  EXPECT_FALSE(made_reservation.load());
  { auto unused = std::move(reservation_a); }
  EXPECT_FALSE(made_reservation.load());

  // Moving assignment and destruction does not release the reservation.
  BlockingRingBufferReservation reservation_c;
  reservation_c = std::move(reservation_b);
  EXPECT_FALSE(made_reservation.load());
  { auto unused = std::move(reservation_b); }
  EXPECT_FALSE(made_reservation.load());

  // Destroying the moved-to object does release the reservation.
  { auto unused = std::move(reservation_c); }

  worker.join();
  EXPECT_TRUE(made_reservation.load());
  EXPECT_EQ(reserve_result.load(), ZX_OK);
  EXPECT_EQ(kBlocks, blocking_reservation.length());
}

}  // namespace
}  // namespace storage
