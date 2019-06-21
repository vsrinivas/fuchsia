// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests Minfs Allocator and AllocatorPromise behavior.

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "allocator.h"

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
    zx_status_t AttachVmo(const zx::vmo& vmo, fuchsia_hardware_block_VmoID* vmoid) final {
        return ZX_OK;
    }
#endif

    void Load(fs::ReadTxn* txn, ReadData data) final {}

    zx_status_t Extend(WriteTxn* txn, WriteData data, GrowMapCallback grow_map) final {
        return ZX_ERR_NO_SPACE;
    }

    uint32_t PoolAvailable() const final { return pool_total_ - pool_used_; }

    uint32_t PoolTotal() const final { return pool_total_; }

    // Write back the allocation of the following items to disk.
    void PersistRange(WriteTxn* txn, WriteData data, size_t index,
                      size_t count) final {}

    void PersistAllocate(WriteTxn* txn, size_t count) final {
        ZX_DEBUG_ASSERT(pool_used_ + count <= pool_total_);
        pool_used_ += static_cast<uint32_t>(count);
    }

    void PersistRelease(WriteTxn* txn, size_t count) final {
        ZX_DEBUG_ASSERT(pool_used_ >= count);
        pool_used_ -= static_cast<uint32_t>(count);
    }

private:
    uint32_t pool_used_;
    uint32_t pool_total_;
};

// Creates an allocator with |kTotalElements| elements.
void CreateAllocator(fbl::unique_ptr<Allocator>* out) {
    // Create an Allocator with FakeStorage.
    // Give it 1 more than total_elements since element 0 will be unavailable.
    fbl::unique_ptr<FakeStorage> storage(new FakeStorage(kTotalElements + 1));
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_OK(Allocator::Create(nullptr, std::move(storage), &allocator));

    // Allocate the '0' index (the Allocator assumes that this is reserved).
    AllocatorPromise zero_promise;
    zero_promise.Initialize(nullptr, 1, allocator.get());
    size_t index = zero_promise.Allocate(nullptr);
    ZX_DEBUG_ASSERT(index == 0);
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);

    *out = std::move(allocator);
}

// Initializes the |promise| with |reserved_count| elements from |allocator|.
// Should only be called if initialization is expected to succeed.
void InitializePromise(size_t reserved_count, Allocator* allocator,
                       AllocatorPromise* promise) {
    ASSERT_FALSE(promise->IsInitialized());
    ASSERT_OK(promise->Initialize(nullptr, reserved_count, allocator));
    ASSERT_TRUE(promise->IsInitialized());
    ASSERT_EQ(promise->GetReserved(), reserved_count);
}

TEST(AllocatorTest, InitializeEmpty) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Initialize an empty AllocatorPromise (with no reserved units);
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
    AllocatorPromise promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(0, allocator.get(), &promise));
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, InitializeSplit) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Initialize an AllocatorPromise with all available units reserved.
    AllocatorPromise full_promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(kTotalElements, allocator.get(), &full_promise));
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Now split the full promise with the uninit promise, and check that it becomes initialized.
    AllocatorPromise uninit_promise;
    full_promise.GiveBlocks(1, &uninit_promise);
    ASSERT_TRUE(uninit_promise.IsInitialized());
    ASSERT_EQ(full_promise.GetReserved(), kTotalElements - 1);
    ASSERT_EQ(uninit_promise.GetReserved(), 1);

    // Cancel the promises.
    uninit_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), 1);
    full_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, InitializeOverReserve) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Attempt to reserve more elements than the allocator has.
    AllocatorPromise promise;
    ASSERT_NOT_OK(promise.Initialize(nullptr, kTotalElements + 1, allocator.get()));
}

TEST(AllocatorTest, InitializeTwiceFails) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    AllocatorPromise promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(1, allocator.get(), &promise));
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements - 1);

    // Attempting to initialize a previously initialized AllocatorPromise should fail.
    ASSERT_NOT_OK(promise.Initialize(nullptr, 1, allocator.get()));

    promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, SplitInitialized) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    uint32_t first_count = kTotalElements / 2;
    uint32_t second_count = kTotalElements - first_count;
    ASSERT_GT(first_count, 0);
    ASSERT_GT(second_count, 0);

    // Initialize an AllocatorPromise with half of the available elements reserved.
    AllocatorPromise first_promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(first_count, allocator.get(), &first_promise));
    ASSERT_EQ(allocator->GetAvailable(), second_count);

    // Initialize a second AllocatorPromise with the remaining elements.
    AllocatorPromise second_promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(second_count, allocator.get(), &second_promise));
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Now split the first promise's reservation with the second.
    first_promise.GiveBlocks(1, &second_promise);
    ASSERT_EQ(second_promise.GetReserved(), second_count + 1);
    ASSERT_EQ(first_promise.GetReserved(), first_count - 1);
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Cancel all promises.
    first_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), first_count - 1);
    second_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

TEST(AllocatorTest, TestSplitUninitialized) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Initialize an AllocatorPromise with all available elements reserved.
    AllocatorPromise first_promise;
    ASSERT_NO_FATAL_FAILURES(InitializePromise(kTotalElements, allocator.get(), &first_promise));
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Give half of the first promise's elements to the uninitialized promise.
    AllocatorPromise second_promise;
    uint32_t second_count = kTotalElements / 2;
    uint32_t first_count = kTotalElements - second_count;
    ASSERT_GT(first_count, 0);
    ASSERT_GT(second_count, 0);
    ASSERT_FALSE(second_promise.IsInitialized());
    first_promise.GiveBlocks(second_count, &second_promise);
    ASSERT_TRUE(second_promise.IsInitialized());
    ASSERT_EQ(second_promise.GetReserved(), second_count);
    ASSERT_EQ(first_promise.GetReserved(), first_count);
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Cancel all promises.
    first_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), first_count);
    second_promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), kTotalElements);
}

fbl::Array<size_t> CreateArray(size_t size) {
    fbl::Array<size_t> array(new size_t[size], size);
    memset(array.get(), 0, sizeof(size_t) * size);
    return array;
}

// Helper which allocates |allocate_count| units through |promise|.
// Allocated indices are returned in |out|.
void PerformAllocate(size_t allocate_count, AllocatorPromise* promise,
                     fbl::Array<size_t>* out) {
    ASSERT_NOT_NULL(promise);
    ASSERT_NOT_NULL(out);
    ASSERT_LE(allocate_count, promise->GetReserved());
    size_t remaining_count = promise->GetReserved() - allocate_count;

    fbl::Array<size_t> indices = CreateArray(allocate_count);

    for (size_t i = 0; i < allocate_count; i++) {
        indices[i] = promise->Allocate(nullptr);
    }

    ASSERT_EQ(promise->GetReserved(), remaining_count);
    *out = std::move(indices);
}

// Helper which swaps |swap_count| units through |promise|. |indices| must contain the units to be
// swapped out (can be 0). These values will be replaced with the newly swapp indices.
void PerformSwap(size_t swap_count, AllocatorPromise* promise, fbl::Array<size_t>* indices) {
    ASSERT_NOT_NULL(promise);
    ASSERT_NOT_NULL(indices);
    ASSERT_GE(indices->size(), swap_count);
    ASSERT_GE(promise->GetReserved(), swap_count);
    size_t remaining_count = promise->GetReserved() - swap_count;

    for (size_t i = 0; i < swap_count; i++) {
        size_t old_index = (*indices)[i];
        (*indices)[i] = promise->Swap(old_index);
    }

    ASSERT_EQ(promise->GetReserved(), remaining_count);

    // Commit the swap.
    promise->SwapCommit(nullptr);
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
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Reserve all of the elements.
    AllocatorPromise promise;
    ASSERT_OK(promise.Initialize(nullptr, kTotalElements, allocator.get()));

    // Allocate half of the promise's reserved elements.
    fbl::Array<size_t> indices;
    ASSERT_NO_FATAL_FAILURES(PerformAllocate(kTotalElements / 2, &promise, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = promise.GetReserved();
    promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    // Free the allocated elements.
    ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, Swap) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Reserve all of the elements.
    AllocatorPromise promise;
    ASSERT_OK(promise.Initialize(nullptr, kTotalElements, allocator.get()));

    // Swap half of the promise's reserved elements.
    size_t swap_count = kTotalElements / 2;
    ASSERT_GT(swap_count, 0);
    fbl::Array<size_t> indices = CreateArray(swap_count);
    ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &promise, &indices));
    ASSERT_EQ(allocator->GetAvailable(), 0);

    // Cancel the remaining reservation.
    size_t reserved_count = promise.GetReserved();
    promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), reserved_count);

    // Free the allocated elements.
    ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, AllocateSwap) {
    fbl::unique_ptr<Allocator> allocator;
    ASSERT_NO_FATAL_FAILURES(CreateAllocator(&allocator));

    // Reserve all of the elements.
    AllocatorPromise promise;
    ASSERT_OK(promise.Initialize(nullptr, kTotalElements, allocator.get()));

    // Allocate half of the promise's reserved elements.
    size_t allocate_count = kTotalElements / 2;
    ASSERT_GT(allocate_count, 0);
    fbl::Array<size_t> indices;
    ASSERT_NO_FATAL_FAILURES(PerformAllocate(allocate_count, &promise, &indices));

    // Swap as many of the allocated elements as possible.
    size_t swap_count = fbl::min(promise.GetReserved(), allocate_count);
    ASSERT_GT(swap_count, 0);
    ASSERT_NO_FATAL_FAILURES(PerformSwap(swap_count, &promise, &indices));

    // Cancel the remaining reservation.
    size_t reserved_count = promise.GetReserved();
    promise.Cancel();
    ASSERT_EQ(allocator->GetAvailable(), swap_count + reserved_count);

    // Free the allocated elements.
    ASSERT_NO_FATAL_FAILURES(PerformFree(allocator.get(), indices));
}

TEST(AllocatorTest, PersistRange) {
    // Create PersistentStorage with bogus attributes - valid storage is unnecessary for this test.
    AllocatorFvmMetadata fvm_metadata;
    AllocatorMetadata metadata(0, 0, false, std::move(fvm_metadata), 0, 0);
    PersistentStorage storage(nullptr, nullptr, kMinfsBlockSize, nullptr, std::move(metadata));
    WriteTxn txn(nullptr);
    ASSERT_EQ(txn.BlockCount(), 0);

    // Add a transaction which crosses the boundary between two blocks within the storage bitmap.
    storage.PersistRange(&txn, 1, kMinfsBlockBits - 1, 2);

    // Check that two distinct blocks have been added to the txn.
    ASSERT_EQ(txn.BlockCount(), 2);
    txn.Cancel();
}

} // namespace
} // namespace minfs
