// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/ring-buffer.h>

#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

// TODO(smklein): This interface is larger than necessary. Can we reduce it
// to just "attach/detach vmo"?
class MockSpaceManager : public SpaceManager {
public:
    vmoid_t default_vmoid() const {
        return 1;
    }

private:
    const Superblock& Info() const final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }
    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) override {
        *out = default_vmoid();
        return ZX_OK;
    }
    zx_status_t DetachVmo(vmoid_t vmoid) override {
        EXPECT_EQ(default_vmoid(), vmoid);
        return ZX_OK;
    }
    zx_status_t AddInodes(fzl::ResizeableVmoMapper* node_map) final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }
    zx_status_t AddBlocks(size_t nblocks, RawBitmap* block_map) final {
        ZX_ASSERT_MSG(false, "Test should not invoke function: %s\n", __FUNCTION__);
    }
};

TEST(RingBufferTest, EmptyRingBuffer) {
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, RingBuffer::Create(&space_manager, 0, "test-buffer", &buffer));
}

TEST(RingBufferTest, MakeRingBuffer) {
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    const size_t kBlocks = 5;
    ASSERT_OK(RingBuffer::Create(&space_manager, kBlocks, "test-buffer", &buffer));
    EXPECT_EQ(kBlocks, buffer->capacity());
}

TEST(RingBufferTest, ReserveOne) {
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    const size_t kBlocks = 5;
    ASSERT_OK(RingBuffer::Create(&space_manager, kBlocks, "test-buffer", &buffer));
    RingBufferReservation reservation;
    EXPECT_EQ(0, reservation.length());
    EXPECT_OK(buffer->Reserve(1, &reservation));
    EXPECT_EQ(space_manager.default_vmoid(), reservation.vmoid());
    EXPECT_EQ(0, reservation.start());
    EXPECT_EQ(1, reservation.length());
}

TEST(RingBufferTest, ReserveMove) {
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    const size_t kBlocks = 5;
    ASSERT_OK(RingBuffer::Create(&space_manager, kBlocks, "test-buffer", &buffer));
    RingBufferReservation reservation_a;
    ASSERT_OK(buffer->Reserve(1, &reservation_a));
    EXPECT_EQ(1, reservation_a.length());

    // Move Construction.
    RingBufferReservation reservation_b(std::move(reservation_a));
    EXPECT_EQ(0, reservation_a.length());
    EXPECT_EQ(1, reservation_b.length());

    // Move Assignment.
    reservation_a = std::move(reservation_b);
    EXPECT_EQ(1, reservation_a.length());
    EXPECT_EQ(0, reservation_b.length());
}

TEST(RingBufferTest, ReserveAndFreeOutOfOrder) {
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    const size_t kBlocks = 10;
    ASSERT_OK(RingBuffer::Create(&space_manager, kBlocks, "test-buffer", &buffer));
    RingBufferReservation reservations[4];
    ASSERT_OK(buffer->Reserve(1, &reservations[0]));
    ASSERT_OK(buffer->Reserve(2, &reservations[1]));
    ASSERT_OK(buffer->Reserve(3, &reservations[2]));
    ASSERT_OK(buffer->Reserve(4, &reservations[3]));

    // Although we would ordinarily prefer to free in the order we allocated:
    // 0, 1, 2, 3
    //
    // We will instead free in the following order:
    // 3, 1, 2, 0

    {
        auto unused = std::move(reservations[3]);
    }
    {
        auto unused = std::move(reservations[1]);
    }
    {
        auto unused = std::move(reservations[2]);
    }

    // No space is actually freed until the reservations are freed in-order.
    RingBufferReservation failed_reservation;
    EXPECT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(1, &failed_reservation));

    {
        auto unused = std::move(reservations[0]);
    }

    // Now ALL the blocks are freed.
    RingBufferReservation reservation;
    EXPECT_OK(buffer->Reserve(kBlocks, &reservation));
}

// Create a test VMO. Write the following pattern to the vmo:
// Block N: value + N
void MakeTestVmo(size_t blocks, int value, zx::vmo* out_vmo) {
    zx::vmo vmo;
    ASSERT_OK(zx::vmo::create(blocks * kBlobfsBlockSize, 0, &vmo));

    uint8_t buf[kBlobfsBlockSize];
    for (size_t i = 0; i < blocks; i++) {
        memset(buf, value++, sizeof(buf));
        ASSERT_OK(vmo.write(buf, i * kBlobfsBlockSize, kBlobfsBlockSize));
    }
    *out_vmo = std::move(vmo);
}

// Check that block |offset| in |vmo| equals |value|.
// Additionally, check that |value| is also set in |addr|.
void CheckVmoEquals(const zx::vmo& vmo, const void* addr, size_t offset, int value) {
    uint8_t buf[kBlobfsBlockSize];
    ASSERT_OK(vmo.read(buf, offset * kBlobfsBlockSize, kBlobfsBlockSize));
    EXPECT_EQ(0, memcmp(buf, addr, kBlobfsBlockSize), "VMO data not equal to addr");
    memset(buf, value, sizeof(buf));
    EXPECT_EQ(0, memcmp(buf, addr, kBlobfsBlockSize), "VMO data not equal to value");
}

// Checks that, for the portion of data accessible in |reservation|, the |operation| is
// accessible at |offset| within the reservation.
void CheckOperationInRingBuffer(const zx::vmo& vmo, RingBufferReservation* reservation,
                                const UnbufferedOperation& operation, size_t offset, int value) {
    for (size_t i = 0; i < operation.op.length; i++) {
        CheckVmoEquals(vmo, reservation->MutableData(i + offset), operation.op.vmo_offset + i,
                       value + static_cast<int>(operation.op.vmo_offset + i));
    }
}

void ReserveAndCopyRequests(const fbl::unique_ptr<RingBuffer>& buffer,
                            fbl::Vector<UnbufferedOperation> requests, RingBufferRequests* out) {
    RingBufferReservation reservation;
    ASSERT_OK(buffer->Reserve(BlockCount(requests), &reservation));
    fbl::Vector<BufferedOperation> buffer_request;
    ASSERT_OK(reservation.CopyRequests(requests, 0, &buffer_request));
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

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo.get());
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = 0;
    operation.op.length = kVmoBlocks;
    builder.Add(operation);

    const size_t kRingBufferBlocks = 5;
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    RingBufferRequests request;
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
    ASSERT_EQ(1, request.Operations().size());
    // Start of RingBuffer.
    EXPECT_EQ(0, request.Operations()[0].op.vmo_offset);
    // Same location on dev.
    EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
    // Same length.
    EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

    EXPECT_EQ(0, request.Reservation()->start());
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

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo.get());
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset = 1;
    operation.op.dev_offset = 2;
    operation.op.length = kVmoBlocks - operation.op.vmo_offset;
    builder.Add(operation);

    const size_t kRingBufferBlocks = 5;
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    RingBufferRequests request;
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
    ASSERT_EQ(1, request.Operations().size());
    // Start of RingBuffer.
    EXPECT_EQ(0, request.Operations()[0].op.vmo_offset);
    // Same location on dev.
    EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
    // Same length.
    EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

    EXPECT_EQ(0, request.Reservation()->start());
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

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operations[2];
    operations[0].vmo = zx::unowned_vmo(vmo.get());
    operations[0].op.type = OperationType::kWrite;
    operations[0].op.vmo_offset = 0;
    operations[0].op.dev_offset = 2;
    operations[0].op.length = 1;
    builder.Add(operations[0]);
    operations[1].vmo = zx::unowned_vmo(vmo.get());
    operations[1].op.type = OperationType::kWrite;
    operations[1].op.vmo_offset = 2;
    operations[1].op.dev_offset = 4;
    operations[1].op.length = 2;
    builder.Add(operations[1]);

    const size_t kRingBufferBlocks = 5;
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    RingBufferRequests request;
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
    ASSERT_EQ(2, request.Operations().size());
    // Start of RingBuffer, and then immediately following the previous request.
    EXPECT_EQ(0, request.Operations()[0].op.vmo_offset);
    EXPECT_EQ(operations[0].op.length, request.Operations()[1].op.vmo_offset);
    // Same location on dev.
    EXPECT_EQ(operations[0].op.dev_offset, request.Operations()[0].op.dev_offset);
    EXPECT_EQ(operations[1].op.dev_offset, request.Operations()[1].op.dev_offset);
    // Same length.
    EXPECT_EQ(operations[0].op.length, request.Operations()[0].op.length);
    EXPECT_EQ(operations[1].op.length, request.Operations()[1].op.length);

    EXPECT_EQ(0, request.Reservation()->start());
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
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operations[2];
    RingBufferRequests requests[2];
    operations[0].vmo = zx::unowned_vmo(vmo.get());
    operations[0].op.type = OperationType::kWrite;
    operations[0].op.vmo_offset = 0;
    operations[0].op.dev_offset = 2;
    operations[0].op.length = 1;
    builder.Add(operations[0]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[0]));

    operations[1].vmo = zx::unowned_vmo(vmo.get());
    operations[1].op.type = OperationType::kWrite;
    operations[1].op.vmo_offset = 2;
    operations[1].op.dev_offset = 4;
    operations[1].op.length = 2;
    builder.Add(operations[1]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[1]));

    ASSERT_EQ(1, requests[0].Operations().size());
    ASSERT_EQ(1, requests[1].Operations().size());

    // Start of RingBuffer, and then immediately following the previous request.
    EXPECT_EQ(0, requests[0].Operations()[0].op.vmo_offset);
    EXPECT_EQ(operations[0].op.length, requests[1].Operations()[0].op.vmo_offset);
    // Same location on dev.
    EXPECT_EQ(operations[0].op.dev_offset, requests[0].Operations()[0].op.dev_offset);
    EXPECT_EQ(operations[1].op.dev_offset, requests[1].Operations()[0].op.dev_offset);
    // Same length.
    EXPECT_EQ(operations[0].op.length, requests[0].Operations()[0].op.length);
    EXPECT_EQ(operations[1].op.length, requests[1].Operations()[0].op.length);

    EXPECT_EQ(0, requests[0].Reservation()->start());
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

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo.get());
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = 0;
    operation.op.length = kVmoBlocks;
    builder.Add(operation);

    const size_t kRingBufferBlocks = 3;
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    RingBufferRequests request;
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(), &request));
    ASSERT_EQ(1, request.Operations().size());
    // Start of RingBuffer.
    EXPECT_EQ(0, request.Operations()[0].op.vmo_offset);
    // Same location on dev.
    EXPECT_EQ(operation.op.dev_offset, request.Operations()[0].op.dev_offset);
    // Same length.
    EXPECT_EQ(operation.op.length, request.Operations()[0].op.length);

    EXPECT_EQ(0, request.Reservation()->start());
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

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo.get());
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset = 0;
    operation.op.dev_offset = 0;
    operation.op.length = kVmoBlocks;
    builder.Add(operation);

    const size_t kRingBufferBlocks = 3;
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    RingBufferRequests request;
    ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));
    ASSERT_EQ(0, request.Operations().size());
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
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operations[3];
    RingBufferRequests requests[3];
    operations[0].vmo = zx::unowned_vmo(vmo.get());
    operations[0].op.type = OperationType::kWrite;
    operations[0].op.vmo_offset = 0;
    operations[0].op.dev_offset = 0;
    operations[0].op.length = 3;
    builder.Add(operations[0]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[0]));

    operations[1].vmo = zx::unowned_vmo(vmo.get());
    operations[1].op.type = OperationType::kWrite;
    operations[1].op.vmo_offset = 3;
    operations[1].op.dev_offset = 3;
    operations[1].op.length = 3;
    builder.Add(operations[1]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[1]));

    operations[2].vmo = zx::unowned_vmo(vmo.get());
    operations[2].op.type = OperationType::kWrite;
    operations[2].op.vmo_offset = 6;
    operations[2].op.dev_offset = 6;
    operations[2].op.length = 3;
    builder.Add(operations[2]);
    ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));

    CheckOperationInRingBuffer(vmo, requests[0].Reservation(), operations[0], 0, seed);
    CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);

    // Releasing the first request makes enough room in the buffer.
    {
        auto released = std::move(requests[0]);
    }
    builder.Add(operations[2]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[2]));
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
    MockSpaceManager space_manager;
    std::unique_ptr<RingBuffer> buffer;
    ASSERT_OK(RingBuffer::Create(&space_manager, kRingBufferBlocks, "test-buffer", &buffer));

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operations[3];
    RingBufferRequests requests[3];
    operations[0].vmo = zx::unowned_vmo(vmo.get());
    operations[0].op.type = OperationType::kWrite;
    operations[0].op.vmo_offset = 0;
    operations[0].op.dev_offset = 0;
    operations[0].op.length = 3;
    builder.Add(operations[0]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[0]));

    operations[1].vmo = zx::unowned_vmo(vmo.get());
    operations[1].op.type = OperationType::kWrite;
    operations[1].op.vmo_offset = 4;
    operations[1].op.dev_offset = 4;
    operations[1].op.length = 1;
    builder.Add(operations[1]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[1]));

    operations[2].vmo = zx::unowned_vmo(vmo.get());
    operations[2].op.type = OperationType::kWrite;
    operations[2].op.vmo_offset = 6;
    operations[2].op.dev_offset = 6;
    operations[2].op.length = 5;
    builder.Add(operations[2]);
    ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(BlockCount(builder.TakeOperations()), nullptr));

    CheckOperationInRingBuffer(vmo, requests[0].Reservation(), operations[0], 0, seed);
    CheckOperationInRingBuffer(vmo, requests[1].Reservation(), operations[1], 0, seed);

    // Releasing the first request makes enough room in the buffer.
    {
        auto released = std::move(requests[0]);
    }
    builder.Add(operations[2]);
    ASSERT_NO_FATAL_FAILURES(ReserveAndCopyRequests(buffer, builder.TakeOperations(),
                                                    &requests[2]));
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
    MockSpaceManager space_manager;
    VmoBuffer vmo_buffer;
    ASSERT_OK(vmo_buffer.Initialize(&space_manager, kRingBufferBlocks, "test-buffer"));
    auto buffer = std::make_unique<RingBuffer>(std::move(vmo_buffer));

    RingBufferReservation reservations[3];
    ASSERT_OK(buffer->Reserve(2, &reservations[0]));
    ASSERT_OK(buffer->Reserve(1, &reservations[1]));

    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operations[3];

    // "A, B"
    operations[0].vmo = zx::unowned_vmo(vmo.get());
    operations[0].op.type = OperationType::kWrite;
    operations[0].op.vmo_offset = 0;
    operations[0].op.dev_offset = 0;
    operations[0].op.length = 2;
    builder.Add(operations[0]);
    fbl::Vector<BufferedOperation> buffer_operation;
    ASSERT_OK(reservations[0].CopyRequests(builder.TakeOperations(), 0, &buffer_operation));

    // "C"
    operations[1].vmo = zx::unowned_vmo(vmo.get());
    operations[1].op.type = OperationType::kWrite;
    operations[1].op.vmo_offset = 2;
    operations[1].op.dev_offset = 2;
    operations[1].op.length = 1;
    builder.Add(operations[1]);
    ASSERT_OK(reservations[1].CopyRequests(builder.TakeOperations(), 0, &buffer_operation));

    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo, reservations[0].MutableData(0), 0, seed));
    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo, reservations[1].MutableData(0), 2, seed + 2));

    ASSERT_EQ(ZX_ERR_NO_SPACE, buffer->Reserve(3, &reservations[2]));
    {
        auto released = std::move(reservations[0]);
    }
    ASSERT_OK(buffer->Reserve(3, &reservations[2]));

    // "D"
    operations[2].vmo = zx::unowned_vmo(vmo.get());
    operations[2].op.type = OperationType::kWrite;
    operations[2].op.vmo_offset = 3;
    operations[2].op.dev_offset = 3;
    operations[2].op.length = 1;
    builder.Add(operations[2]);

    const size_t reservation_offset = 2;
    ASSERT_OK(reservations[2].CopyRequests(builder.TakeOperations(), reservation_offset,
                                           &buffer_operation));

    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo, reservations[1].MutableData(0), 2, seed + 2));
    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo, reservations[2].MutableData(reservation_offset),
                                            3, seed + 3));
}

// Tests manually adding header and footer around a payload.
//
//       VMO 1: [ A, _, C ] (Copied into buffer via MutableData)
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
    MockSpaceManager space_manager;
    VmoBuffer vmo_buffer;
    ASSERT_OK(vmo_buffer.Initialize(&space_manager, kRingBufferBlocks, "test-buffer"));
    auto buffer = std::make_unique<RingBuffer>(std::move(vmo_buffer));

    RingBufferReservation reservation;
    ASSERT_OK(buffer->Reserve(3, &reservation));
    // Write header from source VMO into reservation.
    ASSERT_OK(vmo_a.read(reservation.MutableData(0), 0, kBlobfsBlockSize));
    // Write footer.
    ASSERT_OK(vmo_a.read(reservation.MutableData(2), 2 * kBlobfsBlockSize, kBlobfsBlockSize));

    // Data "B" of the VMO.
    UnbufferedOperationsBuilder builder;
    UnbufferedOperation operation;
    operation.vmo = zx::unowned_vmo(vmo_b.get());
    operation.op.type = OperationType::kWrite;
    operation.op.vmo_offset = 1;
    operation.op.dev_offset = 1;
    operation.op.length = 1;
    builder.Add(operation);
    fbl::Vector<BufferedOperation> buffer_operation;
    ASSERT_OK(reservation.CopyRequests(builder.TakeOperations(), 1, &buffer_operation));
    ASSERT_EQ(1, buffer_operation.size());
    ASSERT_EQ(1, buffer_operation[0].op.vmo_offset);
    ASSERT_EQ(1, buffer_operation[0].op.dev_offset);
    ASSERT_EQ(1, buffer_operation[0].op.length);

    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo_a, reservation.MutableData(0), 0, seed_a));
    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo_b, reservation.MutableData(1), 1, seed_b + 1));
    ASSERT_NO_FATAL_FAILURES(CheckVmoEquals(vmo_a, reservation.MutableData(2), 2, seed_a + 2));
}

} // namespace
} // namespace blobfs
