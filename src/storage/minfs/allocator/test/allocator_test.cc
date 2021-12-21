// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Minfs Allocator and AllocatorReservation behavior.

#include "src/storage/minfs/allocator/allocator.h"

#include <algorithm>
#include <cstddef>
#include <memory>

#include <fbl/array.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/allocator_reservation.h"
#include "src/storage/minfs/format.h"

namespace minfs {
namespace {

constexpr uint32_t kTotalElements = 64;

class FakeStorage : public AllocatorStorage {
 public:
  FakeStorage() = delete;
  FakeStorage(const FakeStorage&) = delete;
  FakeStorage& operator=(const FakeStorage&) = delete;

  FakeStorage(uint32_t units) : pool_used_(0), pool_total_(units) {}

  ~FakeStorage() {}

#ifdef __Fuchsia__
  zx::status<> AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* vmoid) final { return zx::ok(); }
#endif

  void Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) final {}

  zx::status<> Extend(PendingWork* transaction, WriteData data, GrowMapCallback grow_map) final {
    return zx::error(ZX_ERR_NO_SPACE);
  }

  uint32_t PoolAvailable() const final { return pool_total_ - pool_used_; }

  uint32_t PoolTotal() const final { return pool_total_; }

  // Write back the allocation of the following items to disk.
  void PersistRange(PendingWork* transaction, WriteData data, size_t index, size_t count) final {}

  void PersistAllocate(PendingWork* transaction, size_t count) final {
    ZX_DEBUG_ASSERT(pool_used_ + count <= pool_total_);
    pool_used_ += static_cast<uint32_t>(count);
  }

  void PersistRelease(PendingWork* transaction, size_t count) final {
    ZX_DEBUG_ASSERT(pool_used_ >= count);
    pool_used_ -= static_cast<uint32_t>(count);
  }

 private:
  uint32_t pool_used_;
  uint32_t pool_total_;
};

class FakeTransaction : public PendingWork {
 public:
  void EnqueueMetadata(storage::Operation operation, storage::BlockBuffer* buffer) final {
    storage::UnbufferedOperation unbuffered_operation = {.vmo = zx::unowned_vmo(buffer->Vmo()),
                                                         .op = std::move(operation)};
    metadata_operations_.Add(std::move(unbuffered_operation));
  }

  void EnqueueData(storage::Operation operation, storage::BlockBuffer* buffer) final {}

  size_t AllocateBlock() final { return 0; }
  void DeallocateBlock(size_t) final {}

  size_t BlockCount() { return metadata_operations_.BlockCount(); }

 private:
  storage::UnbufferedOperationsBuilder metadata_operations_;
};

// Creates an allocator with |kTotalElements| elements.
void CreateAllocator(std::unique_ptr<Allocator>* out) {
  // Create an Allocator with FakeStorage.
  // Give it 1 more than total_elements since element 0 will be unavailable.
  std::unique_ptr<FakeStorage> storage(new FakeStorage(kTotalElements + 1));
  fs::BufferedOperationsBuilder builder;
  auto allocator_or = Allocator::Create(&builder, std::move(storage));
  ASSERT_TRUE(allocator_or.is_ok());

  // Allocate the '0' index (the Allocator assumes that this is reserved).
  AllocatorReservation zero_reservation(allocator_or.value().get());
  ASSERT_TRUE(zero_reservation.Reserve(nullptr, 1).is_ok());
  size_t index = zero_reservation.Allocate();
  ZX_DEBUG_ASSERT(index == 0);
  ASSERT_EQ(allocator_or->GetAvailable(), kTotalElements);
  FakeTransaction transaction;
  zero_reservation.Commit(&transaction);

  *out = std::move(allocator_or.value());
}

// Initializes the |reservation| with |reserved_count| elements from |allocator|.
// Should only be called if initialization is expected to succeed.
void InitializeReservation(size_t reserved_count, AllocatorReservation* reservation) {
  ASSERT_TRUE(reservation->Reserve(nullptr, reserved_count).is_ok());
  ASSERT_EQ(reservation->GetReserved(), reserved_count);
}

TEST(AllocatorTest, ReserveEmpty) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  // Initialize an empty AllocatorReservation (with no reserved units);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  AllocatorReservation reservation(allocator.get());
  ASSERT_NO_FATAL_FAILURE(InitializeReservation(0, &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, OverReserve) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  // Attempt to reserve more elements than the allocator has.
  AllocatorReservation reservation(allocator.get());
  ASSERT_TRUE(reservation.Reserve(nullptr, kTotalElements + 1).is_error());
}

TEST(AllocatorTest, ReserveTwiceFails) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  AllocatorReservation reservation(allocator.get());
  ASSERT_NO_FATAL_FAILURE(InitializeReservation(1, &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements - 1);

  // Attempting to initialize a previously initialized AllocatorReservation should fail.
  ASSERT_TRUE(reservation.Reserve(nullptr, 1).is_error());

  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, ExtendReservationByZeroDoesNotFail) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  // Initialize an empty AllocatorReservation (with no reserved units);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  AllocatorReservation reservation(allocator.get());
  ASSERT_TRUE(reservation.Reserve(nullptr, 1).is_ok());

  ASSERT_TRUE(reservation.ExtendReservation(nullptr, 0).is_ok());
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements - 1);
}

TEST(AllocatorTest, ExtendReservationByFewBlocks) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));
  constexpr size_t kInitialReservation = 3;
  constexpr size_t kExtendedReservation = 8;

  // Initialize an empty AllocatorReservation (with no reserved units);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  AllocatorReservation reservation(allocator.get());
  ASSERT_TRUE(reservation.Reserve(nullptr, kInitialReservation).is_ok());

  ASSERT_TRUE(reservation.ExtendReservation(nullptr, kExtendedReservation).is_ok());
  ASSERT_EQ(allocator->GetAvailable(),
            kTotalElements - (kInitialReservation + kExtendedReservation));
}

TEST(AllocatorTest, OverExtendFails) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));
  constexpr size_t kInitialReservation = 3;
  constexpr size_t kExtendedReservation = kTotalElements + 1 - kInitialReservation;

  AllocatorReservation reservation(allocator.get());
  ASSERT_TRUE(reservation.Reserve(nullptr, kInitialReservation).is_ok());

  // Attempt to extend reservation more elements than the allocator has.
  ASSERT_TRUE(reservation.ExtendReservation(nullptr, kExtendedReservation).is_error());
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements - kInitialReservation);
}

TEST(AllocatorTest, GetReserved) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));
  constexpr size_t kInitialReservation = 3;
  constexpr size_t kExtendedReservation = 2;
  constexpr size_t kExtendedReservationFail =
      kTotalElements + 1 - (kInitialReservation + kExtendedReservation);

  AllocatorReservation reservation(allocator.get());

  // Nothing should be reserved.
  ASSERT_EQ(reservation.GetReserved(), 0ul);

  // kInitialReservation elements should be reserved.
  ASSERT_TRUE(reservation.Reserve(nullptr, kInitialReservation).is_ok());
  ASSERT_EQ(reservation.GetReserved(), kInitialReservation);

  // kInitialReservation + kExtendedReservation elements should be reserved.
  ASSERT_TRUE(reservation.ExtendReservation(nullptr, kExtendedReservation).is_ok());
  ASSERT_EQ(reservation.GetReserved(), kInitialReservation + kExtendedReservation);

  // Attempt to extend reservation more elements than the allocator has. The reserved elements
  // should be unchanged.
  ASSERT_TRUE(reservation.ExtendReservation(nullptr, kExtendedReservationFail).is_error());
  ASSERT_EQ(reservation.GetReserved(), kInitialReservation + kExtendedReservation);

  // On cancelling reservation, number of reserved elements should be 0.
  reservation.Cancel();
  ASSERT_EQ(reservation.GetReserved(), 0ul);
}

fbl::Array<size_t> CreateArray(size_t size) {
  fbl::Array<size_t> array(new size_t[size], size);
  memset(array.data(), 0, sizeof(size_t) * size);
  return array;
}

// Helper which allocates |allocate_count| units through |reservation|.
// Allocated indices are returned in |out|.
void PerformAllocate(size_t allocate_count, AllocatorReservation* reservation,
                     fbl::Array<size_t>* out) {
  ASSERT_NE(nullptr, reservation);
  ASSERT_NE(nullptr, out);
  ASSERT_LE(allocate_count, reservation->GetReserved());
  size_t remaining_count = reservation->GetReserved() - allocate_count;

  fbl::Array<size_t> indices = CreateArray(allocate_count);

  for (size_t i = 0; i < allocate_count; i++) {
    indices[i] = reservation->Allocate();
  }

  ASSERT_EQ(reservation->GetReserved(), remaining_count);
  *out = std::move(indices);
}

// Helper which swaps |swap_count| units through |reservation|. |indices| must contain the units to
// be swapped out (can be 0). These values will be replaced with the newly swapp indices.
void PerformSwap(size_t swap_count, AllocatorReservation* reservation,
                 fbl::Array<size_t>* indices) {
  ASSERT_NE(nullptr, reservation);
  ASSERT_NE(nullptr, indices);
  ASSERT_GE(indices->size(), swap_count);
  ASSERT_GE(reservation->GetReserved(), swap_count);
  size_t remaining_count = reservation->GetReserved() - swap_count;

  for (size_t i = 0; i < swap_count; i++) {
    size_t old_index = (*indices)[i];
    (*indices)[i] = reservation->Swap(old_index);
  }

  ASSERT_EQ(reservation->GetReserved(), remaining_count);
}

// Frees all units in |indices| from |allocator|.
void PerformFree(Allocator* allocator, const fbl::Array<size_t>& indices) {
  size_t free_count = allocator->GetAvailable();

  {
    AllocatorReservation reservation(allocator);
    for (size_t i = 0; i < indices.size(); i++) {
      allocator->Free(&reservation, indices[i]);
    }
    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  ASSERT_EQ(allocator->GetAvailable(), indices.size() + free_count);
}

void ReserveAndExtend(AllocatorReservation& reservation, size_t elements) {
  size_t extend_by = elements / 2;
  size_t reserve = elements - extend_by;
  ASSERT_TRUE(reservation.Reserve(nullptr, reserve).is_ok());
  ASSERT_TRUE(reservation.ExtendReservation(nullptr, extend_by).is_ok());
  ASSERT_EQ(reservation.GetReserved(), elements);
}

TEST(AllocatorTest, Allocate) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  fbl::Array<size_t> indices;
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ReserveAndExtend(reservation, kTotalElements);

    // Allocate half of the reservation's reserved elements.
    ASSERT_NO_FATAL_FAILURE(PerformAllocate(kTotalElements / 2, &reservation, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURE(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, Swap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  size_t swap_count = kTotalElements / 2;
  fbl::Array<size_t> indices = CreateArray(swap_count);
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ASSERT_TRUE(reservation.Reserve(nullptr, kTotalElements).is_ok());

    // Swap half of the reservation's reserved elements.
    ASSERT_GT(swap_count, 0u);
    ASSERT_NO_FATAL_FAILURE(PerformSwap(swap_count, &reservation, &indices));
    ASSERT_EQ(allocator->GetAvailable(), 0ul);

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURE(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, AllocateSwap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  fbl::Array<size_t> indices;
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ASSERT_TRUE(reservation.Reserve(nullptr, kTotalElements).is_ok());

    // Allocate half of the reservation's reserved elements.
    size_t allocate_count = kTotalElements / 2;
    ASSERT_GT(allocate_count, 0ul);
    ASSERT_NO_FATAL_FAILURE(PerformAllocate(allocate_count, &reservation, &indices));

    // Swap as many of the allocated elements as possible.
    size_t swap_count = std::min(reservation.GetReserved(), allocate_count);
    ASSERT_GT(swap_count, 0ul);
    ASSERT_NO_FATAL_FAILURE(PerformSwap(swap_count, &reservation, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), swap_count + reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURE(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, PersistRange) {
  // Create PersistentStorage with bogus attributes - valid storage is unnecessary for this test.
  AllocatorFvmMetadata fvm_metadata;
  AllocatorMetadata metadata(0, 0, false, std::move(fvm_metadata), nullptr, {});
  PersistentStorage storage(nullptr, nullptr, kMinfsBlockSize, nullptr, std::move(metadata),
                            kMinfsBlockSize);
  FakeTransaction transaction;
  ASSERT_EQ(transaction.BlockCount(), 0ul);

  // Add a transaction which crosses the boundary between two blocks within the storage bitmap.
  storage.PersistRange(&transaction, 1, kMinfsBlockBits - 1, 2);

  // Check that two distinct blocks have been added to the txn.
  ASSERT_EQ(transaction.BlockCount(), 2ul);
}

TEST(AllocatorTest, PendingAllocationIsReserved) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  AllocatorReservation reservation1(allocator.get());
  FakeTransaction transaction;
  ASSERT_TRUE(reservation1.Reserve(&transaction, 1).is_ok());
  size_t item = reservation1.Allocate();

  AllocatorReservation reservation2(allocator.get());
  ASSERT_TRUE(reservation2.Reserve(&transaction, 1).is_ok());
  size_t item2 = reservation2.Allocate();
  EXPECT_NE(item, item2);

  AllocatorReservation reservation3(allocator.get());
  ASSERT_TRUE(reservation3.Reserve(&transaction, 1).is_ok());
  size_t item3 = reservation3.Allocate();
  EXPECT_NE(item, item3);
  EXPECT_NE(item2, item3);
}

TEST(AllocatorTest, PendingDeallocationIsReserved) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURE(CreateAllocator(&allocator));

  size_t item;
  {
    AllocatorReservation reservation(allocator.get());
    FakeTransaction transaction;
    ASSERT_TRUE(reservation.Reserve(&transaction, 1).is_ok());
    item = reservation.Allocate();
    reservation.Commit(&transaction);
  }

  {
    // Free that item.
    AllocatorReservation reservation(allocator.get());
    allocator->Free(&reservation, item);
    FakeTransaction transaction;
    reservation.Commit(&transaction);

    // Even though we have freed the item, we won't reuse it until reservation goes out of scope.
    AllocatorReservation reservation2(allocator.get());
    EXPECT_TRUE(reservation2.Reserve(&transaction, 1).is_ok());
    EXPECT_NE(item, reservation2.Allocate());
  }

  // Now we should be able to allocate that item.
  AllocatorReservation reservation(allocator.get());
  FakeTransaction transaction;
  EXPECT_TRUE(reservation.Reserve(&transaction, 1).is_ok());
  EXPECT_EQ(item, reservation.Allocate());
}

}  // namespace
}  // namespace minfs
