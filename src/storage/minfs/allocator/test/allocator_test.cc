// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Minfs Allocator and AllocatorReservation behavior.

#include "src/storage/minfs/allocator/allocator.h"

#include <algorithm>
#include <memory>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

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
  zx_status_t AttachVmo(const zx::vmo& vmo, storage::OwnedVmoid* vmoid) final { return ZX_OK; }
#endif

  void Load(fs::BufferedOperationsBuilder* builder, storage::BlockBuffer* data) final {}

  zx_status_t Extend(PendingWork* transaction, WriteData data, GrowMapCallback grow_map) final {
    return ZX_ERR_NO_SPACE;
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
  std::unique_ptr<Allocator> allocator;
  fs::BufferedOperationsBuilder builder;
  ASSERT_OK(Allocator::Create(&builder, std::move(storage), &allocator));

  // Allocate the '0' index (the Allocator assumes that this is reserved).
  AllocatorReservation zero_reservation(allocator.get());
  zero_reservation.Reserve(nullptr, 1);
  size_t index = zero_reservation.Allocate();
  ZX_DEBUG_ASSERT(index == 0);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  FakeTransaction transaction;
  zero_reservation.Commit(&transaction);

  *out = std::move(allocator);
}

// Initializes the |reservation| with |reserved_count| elements from |allocator|.
// Should only be called if initialization is expected to succeed.
void InitializeReservation(size_t reserved_count, AllocatorReservation* reservation) {
  ASSERT_OK(reservation->Reserve(nullptr, reserved_count));
  ASSERT_EQ(reservation->GetReserved(), reserved_count);
}

TEST(AllocatorTest, ReserveEmpty) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Initialize an empty AllocatorReservation (with no reserved units);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  AllocatorReservation reservation(allocator.get());
  ASSERT_NO_FATAL_FAILURES(InitializeReservation(0, &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, OverReserve) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Attempt to reserve more elements than the allocator has.
  AllocatorReservation reservation(allocator.get());
  ASSERT_NOT_OK(reservation.Reserve(nullptr, kTotalElements + 1));
}

TEST(AllocatorTest, ReserveTwiceFails) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  AllocatorReservation reservation(allocator.get());
  ASSERT_NO_FATAL_FAILURES(InitializeReservation(1, &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements - 1);

  // Attempting to initialize a previously initialized AllocatorReservation should fail.
  ASSERT_NOT_OK(reservation.Reserve(nullptr, 1));

  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
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
  ASSERT_NOT_NULL(reservation);
  ASSERT_NOT_NULL(out);
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
  ASSERT_NOT_NULL(reservation);
  ASSERT_NOT_NULL(indices);
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

TEST(AllocatorTest, Allocate) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  fbl::Array<size_t> indices;
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ASSERT_OK(reservation.Reserve(nullptr, kTotalElements));

    // Allocate half of the reservation's reserved elements.
    ASSERT_NO_FATAL_FAILURES(PerformAllocate(kTotalElements / 2, &reservation, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, Swap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  size_t swap_count = kTotalElements / 2;
  fbl::Array<size_t> indices = CreateArray(swap_count);
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ASSERT_OK(reservation.Reserve(nullptr, kTotalElements));

    // Swap half of the reservation's reserved elements.
    ASSERT_GT(swap_count, 0);
    ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &reservation, &indices));
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, AllocateSwap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  fbl::Array<size_t> indices;
  {
    // Reserve all of the elements.
    AllocatorReservation reservation(allocator.get());
    ASSERT_OK(reservation.Reserve(nullptr, kTotalElements));

    // Allocate half of the reservation's reserved elements.
    size_t allocate_count = kTotalElements / 2;
    ASSERT_GT(allocate_count, 0);
    ASSERT_NO_FATAL_FAILURES(PerformAllocate(allocate_count, &reservation, &indices));

    // Swap as many of the allocated elements as possible.
    size_t swap_count = std::min(reservation.GetReserved(), allocate_count);
    ASSERT_GT(swap_count, 0);
    ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &reservation, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = reservation.GetReserved();
    reservation.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), swap_count + reserved_count);

    FakeTransaction transaction;
    reservation.Commit(&transaction);
  }

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, PersistRange) {
  // Create PersistentStorage with bogus attributes - valid storage is unnecessary for this test.
  AllocatorFvmMetadata fvm_metadata;
  AllocatorMetadata metadata(0, 0, false, std::move(fvm_metadata), nullptr, {});
  PersistentStorage storage(nullptr, nullptr, kMinfsBlockSize, nullptr, std::move(metadata),
                            kMinfsBlockSize);
  FakeTransaction transaction;
  ASSERT_EQ(transaction.BlockCount(), 0);

  // Add a transaction which crosses the boundary between two blocks within the storage bitmap.
  storage.PersistRange(&transaction, 1, kMinfsBlockBits - 1, 2);

  // Check that two distinct blocks have been added to the txn.
  ASSERT_EQ(transaction.BlockCount(), 2);
}

TEST(AllocatorTest, PendingAllocationIsReserved) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  AllocatorReservation reservation1(allocator.get());
  FakeTransaction transaction;
  ASSERT_OK(reservation1.Reserve(&transaction, 1));
  size_t item = reservation1.Allocate();

  AllocatorReservation reservation2(allocator.get());
  ASSERT_OK(reservation2.Reserve(&transaction, 1));
  size_t item2 = reservation2.Allocate();
  EXPECT_NE(item, item2);

  AllocatorReservation reservation3(allocator.get());
  ASSERT_OK(reservation3.Reserve(&transaction, 1));
  size_t item3 = reservation3.Allocate();
  EXPECT_NE(item, item3);
  EXPECT_NE(item2, item3);
}

TEST(AllocatorTest, PendingDeallocationIsReserved) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  size_t item;
  {
    AllocatorReservation reservation(allocator.get());
    FakeTransaction transaction;
    ASSERT_OK(reservation.Reserve(&transaction, 1));
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
    reservation2.Reserve(&transaction, 1);
    EXPECT_NE(item, reservation2.Allocate());
  }

  // Now we should be able to allocate that item.
  AllocatorReservation reservation(allocator.get());
  FakeTransaction transaction;
  reservation.Reserve(&transaction, 1);
  EXPECT_EQ(item, reservation.Allocate());
}

}  // namespace
}  // namespace minfs
