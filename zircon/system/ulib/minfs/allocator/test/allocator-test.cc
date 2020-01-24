// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Minfs Allocator and AllocatorReservation behavior.

#include "allocator.h"

#include <memory>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

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
  zx_status_t AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoId* vmoid) final {
    return ZX_OK;
  }
#endif

  void Load(fs::ReadTxn* txn, ReadData data) final {}

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

// Creates an allocator with |kTotalElements| elements.
void CreateAllocator(std::unique_ptr<Allocator>* out) {
  // Create an Allocator with FakeStorage.
  // Give it 1 more than total_elements since element 0 will be unavailable.
  std::unique_ptr<FakeStorage> storage(new FakeStorage(kTotalElements + 1));
  std::unique_ptr<Allocator> allocator;
  ASSERT_OK(Allocator::Create(nullptr, std::move(storage), &allocator));

  // Allocate the '0' index (the Allocator assumes that this is reserved).
  AllocatorReservation zero_reservation;
  zero_reservation.Initialize(nullptr, 1, allocator.get());
  size_t index = zero_reservation.Allocate(nullptr);
  ZX_DEBUG_ASSERT(index == 0);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);

  *out = std::move(allocator);
}

// Initializes the |reservation| with |reserved_count| elements from |allocator|.
// Should only be called if initialization is expected to succeed.
void InitializeReservation(size_t reserved_count, Allocator* allocator,
                           AllocatorReservation* reservation) {
  ASSERT_FALSE(reservation->IsInitialized());
  ASSERT_OK(reservation->Initialize(nullptr, reserved_count, allocator));
  ASSERT_TRUE(reservation->IsInitialized());
  ASSERT_EQ(reservation->GetReserved(), reserved_count);
}

TEST(AllocatorTest, InitializeEmpty) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Initialize an empty AllocatorReservation (with no reserved units);
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
  AllocatorReservation reservation;
  ASSERT_NO_FATAL_FAILURES(InitializeReservation(0, allocator.get(), &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, InitializeSplit) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Initialize an AllocatorReservation with all available units reserved.
  AllocatorReservation full_reservation;
  ASSERT_NO_FATAL_FAILURES(
      InitializeReservation(kTotalElements, allocator.get(), &full_reservation));
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Now split the full reservation with the uninit reservation, and check that it becomes
  // initialized.
  AllocatorReservation uninit_reservation;
  full_reservation.GiveBlocks(1, &uninit_reservation);
  ASSERT_TRUE(uninit_reservation.IsInitialized());
  ASSERT_EQ(full_reservation.GetReserved(), kTotalElements - 1);
  ASSERT_EQ(uninit_reservation.GetReserved(), 1);

  // Cancel the reservations.
  uninit_reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), 1);
  full_reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, InitializeOverReserve) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Attempt to reserve more elements than the allocator has.
  AllocatorReservation reservation;
  ASSERT_NOT_OK(reservation.Initialize(nullptr, kTotalElements + 1, allocator.get()));
}

TEST(AllocatorTest, InitializeTwiceFails) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  AllocatorReservation reservation;
  ASSERT_NO_FATAL_FAILURES(InitializeReservation(1, allocator.get(), &reservation));
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements - 1);

  // Attempting to initialize a previously initialized AllocatorReservation should fail.
  ASSERT_NOT_OK(reservation.Initialize(nullptr, 1, allocator.get()));

  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, SplitInitialized) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  uint32_t first_count = kTotalElements / 2;
  uint32_t second_count = kTotalElements - first_count;
  ASSERT_GT(first_count, 0);
  ASSERT_GT(second_count, 0);

  // Initialize an AllocatorReservation with half of the available elements reserved.
  AllocatorReservation first_reservation;
  ASSERT_NO_FATAL_FAILURES(InitializeReservation(first_count, allocator.get(), &first_reservation));
  ASSERT_EQ(allocator->GetAvailable(), second_count);

  // Initialize a second AllocatorReservation with the remaining elements.
  AllocatorReservation second_reservation;
  ASSERT_NO_FATAL_FAILURES(
      InitializeReservation(second_count, allocator.get(), &second_reservation));
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Now split the first reservation's reservation with the second.
  first_reservation.GiveBlocks(1, &second_reservation);
  ASSERT_EQ(second_reservation.GetReserved(), second_count + 1);
  ASSERT_EQ(first_reservation.GetReserved(), first_count - 1);
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Cancel all reservations.
  first_reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), first_count - 1);
  second_reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, TestSplitUninitialized) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Initialize an AllocatorReservation with all available elements reserved.
  AllocatorReservation first_reservation;
  ASSERT_NO_FATAL_FAILURES(
      InitializeReservation(kTotalElements, allocator.get(), &first_reservation));
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Give half of the first reservation's elements to the uninitialized reservation.
  AllocatorReservation second_reservation;
  uint32_t second_count = kTotalElements / 2;
  uint32_t first_count = kTotalElements - second_count;
  ASSERT_GT(first_count, 0);
  ASSERT_GT(second_count, 0);
  ASSERT_FALSE(second_reservation.IsInitialized());
  first_reservation.GiveBlocks(second_count, &second_reservation);
  ASSERT_TRUE(second_reservation.IsInitialized());
  ASSERT_EQ(second_reservation.GetReserved(), second_count);
  ASSERT_EQ(first_reservation.GetReserved(), first_count);
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Cancel all reservations.
  first_reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), first_count);
  second_reservation.Cancel();
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
    indices[i] = reservation->Allocate(nullptr);
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

  // Commit the swap.
  reservation->SwapCommit(nullptr);
}

// Frees all units in |indices| from |allocator|.
void PerformFree(Allocator* allocator, const fbl::Array<size_t>& indices) {
  size_t free_count = allocator->GetAvailable();

  for (size_t i = 0; i < indices.size(); i++) {
    allocator->Free(nullptr, indices[i]);
  }

  ASSERT_EQ(allocator->GetAvailable(), indices.size() + free_count);
}

TEST(AllocatorTest, Allocate) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Reserve all of the elements.
  AllocatorReservation reservation;
  ASSERT_OK(reservation.Initialize(nullptr, kTotalElements, allocator.get()));

  // Allocate half of the reservation's reserved elements.
  fbl::Array<size_t> indices;
  ASSERT_NO_FATAL_FAILURES(PerformAllocate(kTotalElements / 2, &reservation, &indices));

  // Cancel the remaining reservation.
  size_t reserved_count = reservation.GetReserved();
  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), reserved_count);

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, Swap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Reserve all of the elements.
  AllocatorReservation reservation;
  ASSERT_OK(reservation.Initialize(nullptr, kTotalElements, allocator.get()));

  // Swap half of the reservation's reserved elements.
  size_t swap_count = kTotalElements / 2;
  ASSERT_GT(swap_count, 0);
  fbl::Array<size_t> indices = CreateArray(swap_count);
  ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &reservation, &indices));
  ASSERT_EQ(allocator->GetAvailable(), 0);

  // Cancel the remaining reservation.
  size_t reserved_count = reservation.GetReserved();
  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), reserved_count);

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, AllocateSwap) {
  std::unique_ptr<Allocator> allocator;
  ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

  // Reserve all of the elements.
  AllocatorReservation reservation;
  ASSERT_OK(reservation.Initialize(nullptr, kTotalElements, allocator.get()));

  // Allocate half of the reservation's reserved elements.
  size_t allocate_count = kTotalElements / 2;
  ASSERT_GT(allocate_count, 0);
  fbl::Array<size_t> indices;
  ASSERT_NO_FATAL_FAILURES(PerformAllocate(allocate_count, &reservation, &indices));

  // Swap as many of the allocated elements as possible.
  size_t swap_count = fbl::min(reservation.GetReserved(), allocate_count);
  ASSERT_GT(swap_count, 0);
  ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &reservation, &indices));

  // Cancel the remaining reservation.
  size_t reserved_count = reservation.GetReserved();
  reservation.Cancel();
  ASSERT_EQ(allocator->GetAvailable(), swap_count + reserved_count);

  // Free the allocated elements.
  ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

class FakeTransaction : public PendingWork {
 public:
  void EnqueueMetadata(WriteData source, storage::Operation operation) final {
    storage::UnbufferedOperation unbuffered_operation = {.vmo = zx::unowned_vmo(source),
                                                         .op = std::move(operation)};
    metadata_operations_.Add(std::move(unbuffered_operation));
  }

  void EnqueueData(WriteData source, storage::Operation operation) final {}

  size_t BlockCount() { return metadata_operations_.BlockCount(); }

 private:
  storage::UnbufferedOperationsBuilder metadata_operations_;
};

TEST(AllocatorTest, PersistRange) {
  // Create PersistentStorage with bogus attributes - valid storage is unnecessary for this test.
  AllocatorFvmMetadata fvm_metadata;
  AllocatorMetadata metadata(0, 0, false, std::move(fvm_metadata), 0, 0);
  PersistentStorage storage(nullptr, nullptr, kMinfsBlockSize, nullptr, std::move(metadata));
  FakeTransaction transaction;
  ASSERT_EQ(transaction.BlockCount(), 0);

  // Add a transaction which crosses the boundary between two blocks within the storage bitmap.
  storage.PersistRange(&transaction, 1, kMinfsBlockBits - 1, 2);

  // Check that two distinct blocks have been added to the txn.
  ASSERT_EQ(transaction.BlockCount(), 2);
}

}  // namespace
}  // namespace minfs
