// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/ring_buffer.h"

#include <lib/zx/vmo.h>
#include <zircon/assert.h>

#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <storage/operation/unbuffered_operations_builder.h>

namespace storage {
namespace {

const uint32_t kBlockSize = 8192;

class MockVmoidRegistry : public VmoidRegistry {
 public:
  vmoid_t default_vmoid() const { return 1; }

  const zx::vmo& get_vmo() const { return vmo_; }

 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) override {
    *out = Vmoid(default_vmoid());
    EXPECT_EQ(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_), ZX_OK);
    return ZX_OK;
  }
  zx_status_t BlockDetachVmo(Vmoid vmoid) override {
    EXPECT_EQ(default_vmoid(), vmoid.TakeId());
    vmo_.reset();
    return ZX_OK;
  }

  zx::vmo vmo_;
};

TEST(RingBufferTest, EmptyRingBuffer) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            RingBuffer::Create(&vmoid_registry, 0, kBlockSize, "test-buffer", &buffer));
}

TEST(RingBufferTest, MakeRingBuffer) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 5;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  EXPECT_EQ(kBlocks, buffer->capacity());
}

TEST(RingBufferTest, ReserveOne) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 5;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  RingBufferReservation reservation;
  EXPECT_EQ(reservation.length(), 0ul);
  EXPECT_EQ(buffer->Reserve(1, &reservation), ZX_OK);
  EXPECT_EQ(vmoid_registry.default_vmoid(), reservation.vmoid());
  EXPECT_EQ(reservation.start(), 0ul);
  EXPECT_EQ(reservation.length(), 1ul);
}

TEST(RingBufferTest, ReserveMove) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 5;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  RingBufferReservation reservation_a;
  ASSERT_EQ(buffer->Reserve(1, &reservation_a), ZX_OK);
  EXPECT_EQ(reservation_a.length(), 1ul);

  // Move Construction.
  RingBufferReservation reservation_b(std::move(reservation_a));
  EXPECT_EQ(reservation_a.length(), 0ul);
  EXPECT_EQ(reservation_b.length(), 1ul);

  // Move Assignment.
  reservation_a = std::move(reservation_b);
  EXPECT_EQ(reservation_a.length(), 1ul);
  EXPECT_EQ(reservation_b.length(), 0ul);
}

TEST(RingBufferTest, ReservationBufferView) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 5;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  RingBufferReservation reservation_a;
  RingBufferReservation reservation_b;
  EXPECT_EQ(buffer->Reserve(2, &reservation_a), ZX_OK);
  EXPECT_EQ(buffer->Reserve(1, &reservation_b), ZX_OK);
  EXPECT_EQ(vmoid_registry.default_vmoid(), reservation_a.vmoid());
  EXPECT_EQ(vmoid_registry.default_vmoid(), reservation_a.buffer_view().vmoid());

  EXPECT_EQ(reservation_a.start(), 0ul);
  EXPECT_EQ(reservation_a.length(), 2ul);
  EXPECT_EQ(reservation_b.start(), 2ul);
  EXPECT_EQ(reservation_b.length(), 1ul);
}

TEST(RingBufferTest, ReserveAndFreeOutOfOrder) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 10;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  RingBufferReservation reservations[4];
  ASSERT_EQ(buffer->Reserve(1, &reservations[0]), ZX_OK);
  ASSERT_EQ(buffer->Reserve(2, &reservations[1]), ZX_OK);
  ASSERT_EQ(buffer->Reserve(3, &reservations[2]), ZX_OK);
  ASSERT_EQ(buffer->Reserve(4, &reservations[3]), ZX_OK);

  // Although we would ordinarily prefer to free in the order we allocated:
  // 0, 1, 2, 3
  //
  // We will instead free in the following order:
  // 3, 1, 2, 0

  { auto unused = std::move(reservations[3]); }
  { auto unused = std::move(reservations[1]); }
  { auto unused = std::move(reservations[2]); }

  // No space is actually freed until the reservations are freed in-order.
  RingBufferReservation failed_reservation;
  EXPECT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(1, &failed_reservation));

  { auto unused = std::move(reservations[0]); }

  // Now ALL the blocks are freed.
  RingBufferReservation reservation;
  EXPECT_EQ(buffer->Reserve(kBlocks, &reservation), ZX_OK);
}

// Create a test VMO. Write the following pattern to the vmo:
// Block N: value + N
void MakeTestVmo(size_t blocks, int value, zx::vmo* out_vmo) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(blocks * kBlockSize, 0, &vmo), ZX_OK);

  uint8_t buf[kBlockSize];
  for (size_t i = 0; i < blocks; i++) {
    memset(buf, value++, sizeof(buf));
    ASSERT_EQ(vmo.write(buf, i * kBlockSize, kBlockSize), ZX_OK);
  }
  *out_vmo = std::move(vmo);
}

// Check that block |offset| in |vmo| equals |value|.
// Additionally, check that |value| is also set in |addr|.
void CheckVmoEquals(const zx::vmo& vmo, const void* addr, size_t offset, int value) {
  uint8_t buf[kBlockSize];
  ASSERT_EQ(vmo.read(buf, offset * kBlockSize, kBlockSize), ZX_OK);
  EXPECT_EQ(0, memcmp(buf, addr, kBlockSize)) << "VMO data not equal to addr";
  memset(buf, value, sizeof(buf));
  EXPECT_EQ(0, memcmp(buf, addr, kBlockSize)) << "VMO data not equal to value";
}

// Checks that, for the portion of data accessible in |reservation|, the |operation| is
// accessible at |offset| within the reservation.
void CheckOperationInRingBuffer(const zx::vmo& vmo, RingBufferReservation* reservation,
                                const storage::UnbufferedOperation& operation, size_t offset,
                                int value) {
  for (size_t i = 0; i < operation.op.length; i++) {
    CheckVmoEquals(vmo, reservation->Data(i + offset), operation.op.vmo_offset + i,
                   value + static_cast<int>(operation.op.vmo_offset + i));
  }
}

void ReserveAndCopyRequests(const std::unique_ptr<RingBuffer>& buffer,
                            std::vector<storage::UnbufferedOperation> requests,
                            RingBufferRequests* out) {
  RingBufferReservation reservation;
  ASSERT_EQ(buffer->Reserve(BlockCount(requests), &reservation), ZX_OK);
  std::vector<storage::BufferedOperation> buffer_request;
  ASSERT_TRUE(reservation.CopyRequests(requests, 0, &buffer_request).is_ok());
  *out = RingBufferRequests(std::move(buffer_request), std::move(reservation));
}

//    VMO: [ A, B, C ]
//    DEV: [ A, B, C ]
// BUFFER: [ A, B, C, _, _ ]
TEST(RingBufferTest, OneRequestAtOffsetZero) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 3;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = 0;
  operation.op.length = kVmoBlocks;
  builder.Add(operation);

  const size_t kRingBufferBlocks = 5;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  RingBufferRequests request;
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
  ASSERT_EQ(request.Operations().size(), 1ul);
  // Start of RingBuffer.
  EXPECT_EQ(request.Operations()[0].op.vmo_offset, 0ul);
  // Same location on dev.
  EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
  // Same length.
  EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

  EXPECT_EQ(request.Reservation()->start(), 0ul);
  EXPECT_EQ(operation.op.length, request.Reservation()->length());
  CheckOperationInRingBuffer(vmo, request.Reservation(), operation, 0, seed);
}

//    VMO: [ _, A, B ]
//    DEV: [ _, _, A, B ]
// BUFFER: [ A, B, _, _, _ ]
TEST(RingBufferTest, OneRequestAtNonZeroOffset) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 3;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 1;
  operation.op.dev_offset = 2;
  operation.op.length = kVmoBlocks - operation.op.vmo_offset;
  builder.Add(operation);

  const size_t kRingBufferBlocks = 5;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  RingBufferRequests request;
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
  ASSERT_EQ(request.Operations().size(), 1ul);
  // Start of RingBuffer.
  EXPECT_EQ(request.Operations()[0].op.vmo_offset, 0ul);
  // Same location on dev.
  EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
  // Same length.
  EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

  EXPECT_EQ(request.Reservation()->start(), 0ul);
  EXPECT_EQ(operation.op.length, request.Reservation()->length());
  CheckOperationInRingBuffer(vmo, request.Reservation(), operation, 0, seed);
}

//  VMO 1: [ A, _, _, _ ]
//  VMO 2: [ _, _, B, C ]
//    DEV: [ _, _, A, _, B, C ]
// BUFFER: [ A, B, C, _, _ ]
TEST(RingBufferTest, TwoRequestsToTheSameVmoSameReservation) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 4;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operations[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = storage::OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 2;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = storage::OperationType::kWrite;
  operations[1].op.vmo_offset = 2;
  operations[1].op.dev_offset = 4;
  operations[1].op.length = 2;
  builder.Add(operations[1]);

  const size_t kRingBufferBlocks = 5;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  RingBufferRequests request;
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
  ASSERT_EQ(request.Operations().size(), 2ul);
  // Start of RingBuffer, and then immediately following the previous request.
  EXPECT_EQ(request.Operations()[0].op.vmo_offset, 0ul);
  EXPECT_EQ(operations[0].op.length, request.Operations()[1].op.vmo_offset);
  // Same location on dev.
  EXPECT_EQ(operations[0].op.dev_offset, request.Operations()[0].op.dev_offset);
  EXPECT_EQ(operations[1].op.dev_offset, request.Operations()[1].op.dev_offset);
  // Same length.
  EXPECT_EQ(operations[0].op.length, request.Operations()[0].op.length);
  EXPECT_EQ(operations[1].op.length, request.Operations()[1].op.length);

  EXPECT_EQ(request.Reservation()->start(), 0ul);
  EXPECT_EQ(operations[0].op.length + operations[1].op.length, request.Reservation()->length());
  CheckOperationInRingBuffer(vmo, request.Reservation(), operations[0], 0, seed);
  CheckOperationInRingBuffer(vmo, request.Reservation(), operations[1], operations[0].op.length,
                             seed);
}

//  VMO 1: [ A, _, _, _ ]
//  VMO 2: [ _, _, B, C ]
//    DEV: [ _, _, A, _, B, C ]
// BUFFER: [ A, B, C, _, _ ]
TEST(RingBufferTest, TwoRequestsToTheSameVmoDifferentReservations) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 4;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  const size_t kRingBufferBlocks = 5;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operations[2];
  RingBufferRequests requests[2];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = storage::OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 2;
  operations[0].op.length = 1;
  builder.Add(operations[0]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[0]));

  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = storage::OperationType::kWrite;
  operations[1].op.vmo_offset = 2;
  operations[1].op.dev_offset = 4;
  operations[1].op.length = 2;
  builder.Add(operations[1]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[1]));

  ASSERT_EQ(requests[0].Operations().size(), 1ul);
  ASSERT_EQ(requests[1].Operations().size(), 1ul);

  // Start of RingBuffer, and then immediately following the previous request.
  EXPECT_EQ(requests[0].Operations()[0].op.vmo_offset, 0ul);
  EXPECT_EQ(operations[0].op.length, requests[1].Operations()[0].op.vmo_offset);
  // Same location on dev.
  EXPECT_EQ(operations[0].op.dev_offset, requests[0].Operations()[0].op.dev_offset);
  EXPECT_EQ(operations[1].op.dev_offset, requests[1].Operations()[0].op.dev_offset);
  // Same length.
  EXPECT_EQ(operations[0].op.length, requests[0].Operations()[0].op.length);
  EXPECT_EQ(operations[1].op.length, requests[1].Operations()[0].op.length);

  EXPECT_EQ(requests[0].Reservation()->start(), 0ul);
  EXPECT_EQ(operations[0].op.length, requests[1].Reservation()->start());
  EXPECT_EQ(operations[0].op.length, requests[0].Reservation()->length());
  EXPECT_EQ(operations[1].op.length, requests[1].Reservation()->length());

  CheckOperationInRingBuffer(vmo, requests[0].Reservation(), operations[0], 0, seed);
  CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);
}

//    VMO: [ A, B, C ]
//    DEV: [ A, B, C ]
// BUFFER: [ A, B, C ]
TEST(RingBufferTest, OneRequestFullRingBuffer) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 3;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = 0;
  operation.op.length = kVmoBlocks;
  builder.Add(operation);

  const size_t kRingBufferBlocks = 3;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  RingBufferRequests request;
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
  ASSERT_EQ(request.Operations().size(), 1ul);
  // Start of RingBuffer.
  EXPECT_EQ(request.Operations()[0].op.vmo_offset, 0ul);
  // Same location on dev.
  EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
  // Same length.
  EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

  EXPECT_EQ(request.Reservation()->start(), 0ul);
  EXPECT_EQ(operation.op.length, request.Reservation()->length());
  CheckOperationInRingBuffer(vmo, request.Reservation(), operation, 0, seed);
}

//    VMO: [ A, B, C, D ]
//    DEV: [ A, B, C, D ]
// BUFFER: [ <Too Small> ]
TEST(RingBufferTest, OneRequestWithRingBufferFull) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 4;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo.get());
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 0;
  operation.op.dev_offset = 0;
  operation.op.length = kVmoBlocks;
  builder.Add(operation);

  const size_t kRingBufferBlocks = 3;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  RingBufferRequests request;
  ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));
  ASSERT_EQ(request.Operations().size(), 0ul);
}

//  VMO 1: [ A, B, C, _, _, _ ]
//  VMO 2: [ _, _, _, D, E, F ]
//  VMO 3: [ _, _, _, _, _, _, G, H, I ]
//    DEV: [ A, B, C, D, E, F, G, H, I ]
// BUFFER: [ A, B, C, D, E, F ]
// BUFFER: [ <Too Small for third request> ]
// BUFFER: [ _, _, _, D, E, F ]  After completing first request.
// BUFFER: [ G, H, I, D, E, F ]
TEST(RingBufferTest, RingBufferWraparoundCleanly) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 9;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  const size_t kRingBufferBlocks = 6;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operations[3];
  RingBufferRequests requests[3];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = storage::OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 3;
  builder.Add(operations[0]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[0]));

  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = storage::OperationType::kWrite;
  operations[1].op.vmo_offset = 3;
  operations[1].op.dev_offset = 3;
  operations[1].op.length = 3;
  builder.Add(operations[1]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[1]));

  operations[2].vmo = zx::unowned_vmo(vmo.get());
  operations[2].op.type = storage::OperationType::kWrite;
  operations[2].op.vmo_offset = 6;
  operations[2].op.dev_offset = 6;
  operations[2].op.length = 3;
  builder.Add(operations[2]);
  ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));

  CheckOperationInRingBuffer(vmo, requests[0].Reservation(), operations[0], 0, seed);
  CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);

  // Releasing the first request makes enough room in the buffer.
  { auto released = std::move(requests[0]); }
  builder.Add(operations[2]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[2]));
  CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);
  CheckOperationInRingBuffer(vmo, requests[2].Reservation(), operations[2], 0, seed);
}

//  VMO 1: [ A, B, C, _, _, _ ]
//  VMO 2: [ _, _, _, _, D, _, _ ]
//  VMO 3: [ _, _, _, _, _, _, E, F, G, H, I]
//    DEV: [ A, B, C, _, D, _, E, F, G, H, I]
// BUFFER: [ A, B, C, D, _, _ ]
// BUFFER: [ _, _, _, D, _, _ ]  After completing first request.
// BUFFER: [ G, H, I, D, E, F ]
TEST(RingBufferTest, RingBufferWraparoundSplitRequest) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 11;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  const size_t kRingBufferBlocks = 6;
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  ASSERT_EQ(
      RingBuffer::Create(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer", &buffer),
      ZX_OK);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operations[3];
  RingBufferRequests requests[3];
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = storage::OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 3;
  builder.Add(operations[0]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[0]));

  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = storage::OperationType::kWrite;
  operations[1].op.vmo_offset = 4;
  operations[1].op.dev_offset = 4;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[1]));

  operations[2].vmo = zx::unowned_vmo(vmo.get());
  operations[2].op.type = storage::OperationType::kWrite;
  operations[2].op.vmo_offset = 6;
  operations[2].op.dev_offset = 6;
  operations[2].op.length = 5;
  builder.Add(operations[2]);
  ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));

  CheckOperationInRingBuffer(vmo, requests[0].Reservation(), operations[0], 0, seed);
  CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);

  // Releasing the first request makes enough room in the buffer.
  { auto released = std::move(requests[0]); }
  builder.Add(operations[2]);
  ASSERT_NO_FATAL_FAILURE(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &requests[2]));
  CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);
  CheckOperationInRingBuffer(vmo, requests[2].Reservation(), operations[2], 0, seed);
}

// Tests copying requests at an offset, where the offset wraps around the ring buffer.
//
// RESERVATION 1: [ A, B, _, _ ]
// RESERVATION 2: [ _, _, C, _ ]
// RESERVATION 3: [ _, _, _, D ]
//   RING-BUFFER: [ A, B, C, _ ]
//   RING-BUFFER: [ _, _, C, _ ] After releasing first request.
//   RING-BUFFER: [ _, D, C, _ ] Writing "VMO 3" at an offset within the reservation.
TEST(RingBufferTest, CopyRequestAtOffsetWraparound) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 4;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  const size_t kRingBufferBlocks = 4;
  MockVmoidRegistry vmoid_registry;
  VmoBuffer vmo_buffer;
  ASSERT_EQ(vmo_buffer.Initialize(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer"),
            ZX_OK);
  auto buffer = std::make_unique<RingBuffer>(std::move(vmo_buffer));

  RingBufferReservation reservations[3];
  ASSERT_EQ(buffer->Reserve(2, &reservations[0]), ZX_OK);
  ASSERT_EQ(buffer->Reserve(1, &reservations[1]), ZX_OK);

  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operations[3];

  // "A, B"
  operations[0].vmo = zx::unowned_vmo(vmo.get());
  operations[0].op.type = storage::OperationType::kWrite;
  operations[0].op.vmo_offset = 0;
  operations[0].op.dev_offset = 0;
  operations[0].op.length = 2;
  builder.Add(operations[0]);
  std::vector<storage::BufferedOperation> buffer_operation;
  ASSERT_TRUE(reservations[0].CopyRequests(builder.TakeOperations(), 0, &buffer_operation).is_ok());

  // "C"
  operations[1].vmo = zx::unowned_vmo(vmo.get());
  operations[1].op.type = storage::OperationType::kWrite;
  operations[1].op.vmo_offset = 2;
  operations[1].op.dev_offset = 2;
  operations[1].op.length = 1;
  builder.Add(operations[1]);
  ASSERT_TRUE(reservations[1].CopyRequests(builder.TakeOperations(), 0, &buffer_operation).is_ok());

  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo, reservations[0].Data(0), 0, seed));
  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo, reservations[1].Data(0), 2, seed + 2));

  ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(3, &reservations[2]));
  { auto released = std::move(reservations[0]); }
  ASSERT_EQ(buffer->Reserve(3, &reservations[2]), ZX_OK);

  // "D"
  operations[2].vmo = zx::unowned_vmo(vmo.get());
  operations[2].op.type = storage::OperationType::kWrite;
  operations[2].op.vmo_offset = 3;
  operations[2].op.dev_offset = 3;
  operations[2].op.length = 1;
  builder.Add(operations[2]);

  const size_t reservation_offset = 2;
  ASSERT_TRUE(reservations[2]
                  .CopyRequests(builder.TakeOperations(), reservation_offset, &buffer_operation)
                  .is_ok());

  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo, reservations[1].Data(0), 2, seed + 2));
  ASSERT_NO_FATAL_FAILURE(
      CheckVmoEquals(vmo, reservations[2].Data(reservation_offset), 3, seed + 3));
}

// Tests manually adding header and footer around a payload.
//
//       VMO 1: [ A, _, C ] (Copied into buffer via Data)
//       VMO 2: [ _, B, _ ] (Copied into buffer via CopyRequests)
//  VMO-BUFFER: [ A, B, C ]
//         DEV: [ A, B, C ]
// RING-BUFFER: [ A, B, C ]
TEST(RingBufferTest, CopyRequestAtOffsetWithHeaderAndFooter) {
  zx::vmo vmo_a, vmo_b;
  const size_t kVmoBlocks = 3;
  int seed_a = 0xAB;
  MakeTestVmo(kVmoBlocks, seed_a, &vmo_a);
  int seed_b = 0xCD;
  MakeTestVmo(kVmoBlocks, seed_b, &vmo_b);

  const size_t kRingBufferBlocks = 5;
  MockVmoidRegistry vmoid_registry;
  VmoBuffer vmo_buffer;
  ASSERT_EQ(vmo_buffer.Initialize(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer"),
            ZX_OK);
  auto buffer = std::make_unique<RingBuffer>(std::move(vmo_buffer));

  RingBufferReservation reservation;
  ASSERT_EQ(buffer->Reserve(3, &reservation), ZX_OK);
  // Write header from source VMO into reservation.
  ASSERT_EQ(vmo_a.read(reservation.Data(0), 0, kBlockSize), ZX_OK);
  // Write footer.
  ASSERT_EQ(vmo_a.read(reservation.Data(2), 2 * kBlockSize, kBlockSize), ZX_OK);

  // Data "B" of the VMO.
  storage::UnbufferedOperationsBuilder builder;
  storage::UnbufferedOperation operation;
  operation.vmo = zx::unowned_vmo(vmo_b.get());
  operation.op.type = storage::OperationType::kWrite;
  operation.op.vmo_offset = 1;
  operation.op.dev_offset = 1;
  operation.op.length = 1;
  builder.Add(operation);
  std::vector<storage::BufferedOperation> buffer_operation;
  ASSERT_TRUE(reservation.CopyRequests(builder.TakeOperations(), 1, &buffer_operation).is_ok());
  ASSERT_EQ(buffer_operation.size(), 1ul);
  ASSERT_EQ(buffer_operation[0].op.vmo_offset, 1u);
  ASSERT_EQ(buffer_operation[0].op.dev_offset, 1ul);
  ASSERT_EQ(buffer_operation[0].op.length, 1ul);

  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo_a, reservation.Data(0), 0, seed_a));
  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo_b, reservation.Data(1), 1, seed_b + 1));
  ASSERT_NO_FATAL_FAILURE(CheckVmoEquals(vmo_a, reservation.Data(2), 2, seed_a + 2));
}

TEST(RingBufferTest, ReleaseReservationDecommitsMemory) {
  zx::vmo vmo;
  const size_t kVmoBlocks = 1;
  int seed = 0xAB;
  MakeTestVmo(kVmoBlocks, seed, &vmo);

  const size_t kRingBufferBlocks = 128;
  MockVmoidRegistry vmoid_registry;
  VmoBuffer vmo_buffer;
  ASSERT_EQ(vmo_buffer.Initialize(&vmoid_registry, kRingBufferBlocks, kBlockSize, "test-buffer"),
            ZX_OK);
  auto buffer = std::make_unique<RingBuffer>(std::move(vmo_buffer));

  zx_info_vmo_t info;
  auto write_blocks = [&](size_t count) {
    RingBufferReservation reservation;
    ASSERT_EQ(buffer->Reserve(count, &reservation), ZX_OK);
    // Write header from the source VMO into the reservation.
    for (size_t i = 0; i < count; i++) {
      ASSERT_EQ(vmo.read(reservation.Data(i), 0, kBlockSize), ZX_OK);
    }

    ASSERT_EQ(vmoid_registry.get_vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr),
              ZX_OK);
    ASSERT_EQ(count * kBlockSize, info.committed_bytes);
  };
  // First issue a write that uses half of the buffer.
  write_blocks(kRingBufferBlocks / 2);
  // Now issue a write that uses all of the buffer, which should test wraparound.
  write_blocks(kRingBufferBlocks);

  // All committed bytes of the buffer should be released after the reservation
  // goes out of scope.
  ASSERT_EQ(vmoid_registry.get_vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr),
            ZX_OK);
  ASSERT_EQ(info.committed_bytes, 0ul);
}

TEST(RingBufferTest, ReserveZeroBlocksReturnsError) {
  MockVmoidRegistry vmoid_registry;
  std::unique_ptr<RingBuffer> buffer;
  const size_t kBlocks = 5;
  ASSERT_EQ(RingBuffer::Create(&vmoid_registry, kBlocks, kBlockSize, "test-buffer", &buffer),
            ZX_OK);
  RingBufferReservation reservation;
  EXPECT_EQ(buffer->Reserve(0, &reservation), ZX_ERR_INVALID_ARGS);
}

}  // namespace
}  // namespace storage
