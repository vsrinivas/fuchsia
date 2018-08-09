// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/rights.h>

#include "vmo-probe.h"

// Things to test:
// 1) Init with vmos, init with non-initialized vmos
// 2) memset at address, for size()
// 3) Get a bunch of buffers, make sure it runs out
// 4) Call GetNewBuffer twice, assert fail
// 5) pass bad buffer index to BufferRelease
// 6) try to release twice
// 7) Check GetNewBuffer and BufferCompleted return the same

namespace {

static constexpr size_t kVmoTestSize = 512 << 10;    // 512KB
static constexpr size_t kNumVmos = 20;               // 512KB

// Create vmos for each handle in an array of vmo handles:
bool AssignVmos(size_t num_vmos, size_t vmo_size, zx::vmo* vmos) {
    BEGIN_TEST;
    zx_status_t status;
    for (size_t i = 0; i < num_vmos; ++i) {
        status = zx::vmo::create(vmo_size, 0, &vmos[i]);
        ASSERT_EQ(status, ZX_OK);
    }
    END_TEST;
}

// A helper class to initialize the VmoPool, and to check the state.
// Since we cannot access the VmoPool's free buffer list, we check the
// state of the VmoPool by filling it up and emptying it out.
class VmoPoolTester {
public:
    zx::vmo vmo_handles_[kNumVmos];
    fzl::VmoPool pool_;

    bool Init() {
        BEGIN_TEST;
        ASSERT_TRUE(AssignVmos(kNumVmos, kVmoTestSize, vmo_handles_));
        ASSERT_EQ(pool_.Init(vmo_handles_, kNumVmos), ZX_OK);
        END_TEST;
    }

    bool FillBuffers(size_t num_buffers) {
        BEGIN_TEST;
        for (size_t i = 0; i < kNumVmos && i < num_buffers; ++i) {
            ASSERT_EQ(pool_.GetNewBuffer(nullptr), ZX_OK);
            ASSERT_EQ(pool_.BufferCompleted(nullptr), ZX_OK);
        }
        END_TEST;
    }

    // Fills the pool, to make sure all accounting
    // is done correctly.
    // filled_count is the number of buffers that are already reserved.
    bool CheckFillingPool(size_t filled_count) {
        BEGIN_TEST;
        // Test that the pool gives indexes from 0 to kNumVmos-1
        // It is not required to give the indexes in any particular
        // order.
        bool gave_index[kNumVmos]; // initialized to false
        for (size_t i = 0; i < kNumVmos; ++i) {
            gave_index[i] = false;
        }
        for (size_t i = 0; i < kNumVmos - filled_count; ++i) {
            uint32_t new_buffer_index, buffer_completed_index;
            ASSERT_EQ(pool_.GetNewBuffer(&new_buffer_index), ZX_OK);
            ASSERT_LT(new_buffer_index, kNumVmos);
            ASSERT_EQ(gave_index[new_buffer_index], false);
            gave_index[new_buffer_index] = true;
            ASSERT_TRUE(CheckHasBuffer());
            // Now mark as complete:
            ASSERT_EQ(pool_.BufferCompleted(&buffer_completed_index), ZX_OK);
            // Make sure the index passed on BufferComplete is still the same:
            ASSERT_EQ(new_buffer_index, buffer_completed_index);
            ASSERT_TRUE(CheckHasNoBuffer());
        }
        // Now check that requesting any further buffers fails:
        ASSERT_EQ(pool_.GetNewBuffer(nullptr), ZX_ERR_NOT_FOUND);
        END_TEST;
    }

    // Empties the pool, to make sure all accounting
    // is done correctly.
    // unfilled_count is the number of buffers that are already reserved.
    bool CheckEmptyPool(size_t unfilled_count) {
        BEGIN_TEST;
        size_t release_errors = 0;
        for (uint32_t i = 0; i < kNumVmos; ++i) {
            if (pool_.BufferRelease(i) == ZX_ERR_NOT_FOUND) {
                release_errors++;
                ASSERT_LE(release_errors, unfilled_count);
            }
        }
        // Make sure we had exactly filled_count buffers already released.
        ASSERT_EQ(unfilled_count, release_errors);
        // Now, make sure all buffers are now released.
        for (uint32_t i = 0; i < kNumVmos; ++i) {
            ASSERT_EQ(pool_.BufferRelease(i), ZX_ERR_NOT_FOUND);
        }
        END_TEST;
    }

    bool CheckHasBuffer() {
        BEGIN_TEST;
        ASSERT_TRUE(pool_.HasBufferInProgress());
        void* addr = pool_.CurrentBufferAddress();
        ASSERT_NONNULL(addr);
        size_t mem_size = pool_.CurrentBufferSize();
        ASSERT_EQ(mem_size, kVmoTestSize);
        uint32_t rw_access = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
        ASSERT_TRUE(vmo_probe::probe_verify_region(addr, mem_size, rw_access));
        END_TEST;
    }

    bool CheckHasNoBuffer() {
        BEGIN_TEST;
        ASSERT_FALSE(pool_.HasBufferInProgress());
        void* addr = pool_.CurrentBufferAddress();
        ASSERT_NULL(addr);
        size_t mem_size = pool_.CurrentBufferSize();
        ASSERT_EQ(mem_size, 0);
        END_TEST;
    }

    bool CheckAccounting(bool buffer_in_progress, size_t filled_count) {
        BEGIN_TEST;
        if (buffer_in_progress) {
            ASSERT_TRUE(CheckHasBuffer());
            ASSERT_EQ(pool_.BufferCompleted(), ZX_OK);
            filled_count++;
        }
        ASSERT_TRUE(CheckHasNoBuffer());
        ASSERT_TRUE(CheckFillingPool(filled_count));
        ASSERT_TRUE(CheckEmptyPool(0));
        END_TEST;
    }

    // Shuffles the free list, psuedo-randomly.
    // Assumes that the pool is empty.
    // This shuffle function relies on the fact that if you have a prime
    // number p and a number (n) that does not have that prime number
    // as a factor, the set of (x*p)%n, where x := {0,n-1} will cover the
    // range of {0,n-1} exactly.
    bool ShufflePool() {
        BEGIN_TEST;
        ASSERT_TRUE(FillBuffers(kNumVmos));
        static constexpr uint32_t kHashingSeed = 7;
        static_assert(kNumVmos % kHashingSeed != 0, "Bad Hashing seed");
        uint32_t hashing_index = 0;
        for (size_t i = 0; i < kNumVmos; ++i) {
            hashing_index = (hashing_index + kHashingSeed) % kNumVmos;
            ASSERT_EQ(pool_.BufferRelease(hashing_index), ZX_OK);
        }
        END_TEST;
    }
};

// Initialize the pool with a vector.
// (All the other tests initialize with an array)
// First tries vector of invalid handles
// then assigns the handles, and tries again.
// This test also verifies that you can re-initialize if a previous call to
// Init fails.
bool vmo_pool_init_vector_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    fbl::Vector<zx::vmo> vmo_vector;
    fbl::AllocChecker ac;
    // Move the tester's vmos into a vector:
    for (size_t i = 0; i < kNumVmos; ++i) {
        vmo_vector.push_back(fbl::move(tester.vmo_handles_[i]), &ac);
        ASSERT_TRUE(ac.check());
    }
    ASSERT_NE(tester.pool_.Init(vmo_vector), ZX_OK);
    // Now assign the vmos:
    ASSERT_TRUE(AssignVmos(kNumVmos, kVmoTestSize, vmo_vector.get()));
    ASSERT_EQ(tester.pool_.Init(vmo_vector), ZX_OK);

    ASSERT_TRUE(tester.CheckAccounting(false, 0));
    END_TEST;
}

bool vmo_pool_fill_and_empty_pool_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());
    ASSERT_TRUE(tester.CheckAccounting(false, 0));
    END_TEST;
}

bool vmo_pool_double_get_buffer_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());
    ASSERT_EQ(tester.pool_.GetNewBuffer(), ZX_OK);
    ASSERT_EQ(tester.pool_.GetNewBuffer(), ZX_ERR_BAD_STATE);

    // Now check accounting:
    ASSERT_TRUE(tester.CheckAccounting(true, 0));
    END_TEST;
}

// Checks that you can cancel a buffer, which will put it back into the pool.
bool vmo_pool_release_before_complete_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());
    uint32_t current_buffer;
    ASSERT_EQ(tester.pool_.GetNewBuffer(&current_buffer), ZX_OK);
    ASSERT_EQ(tester.pool_.BufferRelease(current_buffer), ZX_OK);
    ASSERT_TRUE(tester.CheckHasNoBuffer());
    // Running BufferCompleted should now fail, because we did not have an
    // in-progress buffer.
    ASSERT_EQ(tester.pool_.BufferCompleted(&current_buffer), ZX_ERR_BAD_STATE);

    // Now check accounting:
    ASSERT_TRUE(tester.CheckAccounting(false, 0));
    END_TEST;
}

bool vmo_pool_release_wrong_buffer_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());

    uint32_t current_buffer;
    ASSERT_EQ(tester.pool_.GetNewBuffer(&current_buffer), ZX_OK);
    ASSERT_EQ(tester.pool_.BufferCompleted(&current_buffer), ZX_OK);
    // Test an out-of-bounds index:
    ASSERT_EQ(tester.pool_.BufferRelease(kNumVmos), ZX_ERR_INVALID_ARGS);
    // Test all of the buffer indices that are not locked:
    for (uint32_t i = 0; i < kNumVmos; ++i) {
        if (i == current_buffer) {
            continue;
        }
        ASSERT_EQ(tester.pool_.BufferRelease(i), ZX_ERR_NOT_FOUND);
    }
    // Now check accounting:
    ASSERT_TRUE(tester.CheckAccounting(false, 1));
    END_TEST;
}

// Checks that the pool does not need to be amptied or filled in any particular
// order.
bool vmo_pool_out_of_order_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());
    ASSERT_TRUE(tester.ShufflePool());
    // Now check accounting:
    ASSERT_TRUE(tester.CheckAccounting(false, 0));
    END_TEST;
}

bool vmo_pool_reset_test() {
    BEGIN_TEST;
    VmoPoolTester tester;
    ASSERT_TRUE(tester.Init());
    size_t test_cases[]{0, 1, kNumVmos / 2, kNumVmos};
    for (size_t buffer_count : test_cases) {
        // With no buffer in progress
        ASSERT_TRUE(tester.FillBuffers(buffer_count));
        tester.pool_.Reset();
        ASSERT_TRUE(tester.CheckAccounting(false, 0));
        // With buffer in progress:
        if (buffer_count != kNumVmos) {
            ASSERT_TRUE(tester.FillBuffers(buffer_count));
            ASSERT_EQ(tester.pool_.GetNewBuffer(), ZX_OK);
            tester.pool_.Reset();
            ASSERT_TRUE(tester.CheckAccounting(false, 0));
        }
    }
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(vmo_pool_tests)
RUN_NAMED_TEST("vmo_pool_reset", vmo_pool_reset_test)
RUN_NAMED_TEST("vmo_pool_init_vector", vmo_pool_init_vector_test)
RUN_NAMED_TEST("vmo_pool_double_get_buffer", vmo_pool_double_get_buffer_test)
RUN_NAMED_TEST("vmo_pool_release_wrong_buffer", vmo_pool_release_wrong_buffer_test)
RUN_NAMED_TEST("vmo_pool_release_before_complete", vmo_pool_release_before_complete_test)
RUN_NAMED_TEST("vmo_pool_fill_and_empty_pool", vmo_pool_fill_and_empty_pool_test)
RUN_NAMED_TEST("vmo_pool_out_of_order", vmo_pool_out_of_order_test)
END_TEST_CASE(vmo_pool_tests)
