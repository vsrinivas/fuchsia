// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fzl/vmo-pool.h>
#include <lib/zx/vmo.h>
#include <zircon/rights.h>

#include <utility>

#include <zxtest/zxtest.h>

#include "vmo-probe.h"

// Things to test:
// 1) Init with vmos, init with non-initialized vmos
// 2) memset at address, for size()
// 3) Get a bunch of buffers, make sure it runs out
// 4) Call LockBufferForWrite twice, assert fail
// 5) pass bad buffer index to ReleaseBuffer
// 6) try to release twice
// 7) Check LockBufferForWrite and BufferCompleted return the same

namespace {

static constexpr size_t kVmoTestSize = 512 << 10;  // 512KB
static constexpr uint32_t kNumVmos = 20;

// Create vmos for each handle in an array of vmo handles:
void AssignVmos(size_t num_vmos, size_t vmo_size, zx::vmo* vmos) {
  zx_status_t status;
  for (size_t i = 0; i < num_vmos; ++i) {
    status = zx::vmo::create(vmo_size, 0, &vmos[i]);
    EXPECT_OK(status);
  }
}

// A helper class to initialize the VmoPool, and to check the state.
// Since we cannot access the VmoPool's free buffer list, we check the
// state of the VmoPool by filling it up and emptying it out.
class VmoPoolTester : public zxtest::Test {
 public:
  zx::vmo vmo_handles_[kNumVmos];
  fzl::VmoPool pool_;
  bool is_mapped_ = false;
  bool is_pinned_ = false;

  void Init() {
    AssignVmos(kNumVmos, kVmoTestSize, vmo_handles_);
    ASSERT_OK(pool_.Init(vmo_handles_, kNumVmos));
  }

  void SetUp() override { ASSERT_OK(fake_bti_create(bti_.reset_and_get_address())); }

  void FillBuffers(size_t num_buffers) {
    for (size_t i = 0; i < kNumVmos && i < num_buffers; ++i) {
      auto buffer = pool_.LockBufferForWrite();
      ASSERT_TRUE(buffer.has_value());
      __UNUSED uint32_t index = buffer->ReleaseWriteLockAndGetIndex();
    }
  }

  // Create vmos for each handle in an array of vmo handles:
  void CreateContiguousVmos(size_t num_vmos, size_t vmo_size, zx::vmo* vmos) {
    for (size_t i = 0; i < num_vmos; ++i) {
      zx_status_t status = zx::vmo::create_contiguous(bti_, vmo_size, 0, &vmos[i]);
      ASSERT_OK(status);
    }
  }

  void InitContiguous() {
    CreateContiguousVmos(kNumVmos, kVmoTestSize, vmo_handles_);
    ASSERT_OK(pool_.Init(vmo_handles_, kNumVmos));
  }

  void PinVmos(fzl::VmoPool::RequireContig require_contiguous,
               fzl::VmoPool::RequireLowMem require_low_memory) {
    EXPECT_EQ(pool_.PinVmos(bti_, require_contiguous, require_low_memory), ZX_OK);
    is_pinned_ = true;
  }

  void MapVmos() {
    EXPECT_OK(pool_.MapVmos());
    is_mapped_ = true;
  }

  // Fills the pool, to make sure all accounting
  // is done correctly.
  // filled_count is the number of buffers that are already reserved.
  void CheckFillingPool(size_t filled_count) {
    // Test that the pool gives indexes from 0 to kNumVmos-1
    // It is not required to give the indexes in any particular
    // order.
    bool gave_index[kNumVmos];  // initialized to false
    for (size_t i = 0; i < kNumVmos; ++i) {
      gave_index[i] = false;
    }
    for (size_t i = 0; i < kNumVmos - filled_count; ++i) {
      auto buffer = pool_.LockBufferForWrite();
      ASSERT_TRUE(buffer.has_value());
      CheckValidBuffer(*buffer);
      uint32_t buffer_index = buffer->ReleaseWriteLockAndGetIndex();
      CheckInvalidBuffer(*buffer);

      ASSERT_LT(buffer_index, kNumVmos);
      EXPECT_EQ(gave_index[buffer_index], false);
      gave_index[buffer_index] = true;
    }
    // Now check that requesting any further buffers fails:
    auto buffer = pool_.LockBufferForWrite();
    EXPECT_FALSE(buffer.has_value());
  }

  // Check the Buffer to make sure it gives the correct info
  void CheckValidBuffer(fzl::VmoPool::Buffer& buffer) {
    ASSERT_TRUE(buffer.valid());
    EXPECT_EQ(buffer.size(), kVmoTestSize);
    if (is_mapped_) {
      ASSERT_NO_DEATH(([&buffer] { EXPECT_NE(buffer.virtual_address(), nullptr); }));
    } else {
      ASSERT_DEATH(([&buffer] { buffer.virtual_address(); }));
    }
    if (is_pinned_) {
      // Cannot assume that the physical address will be non-zero, since
      // fake-bti returns physical addresses of 0.
      ASSERT_NO_DEATH(([&buffer] { buffer.physical_address(); }));
    } else {
      ASSERT_DEATH(([&buffer] { buffer.physical_address(); }));
    }
  }

  // Check that an invalid buffer acts according to spec.
  void CheckInvalidBuffer(fzl::VmoPool::Buffer& buffer) {
    EXPECT_FALSE(buffer.valid());
    ASSERT_DEATH(([&buffer] { buffer.size(); }));
    ASSERT_DEATH(([&buffer] { buffer.virtual_address(); }));
    ASSERT_DEATH(([&buffer] { buffer.physical_address(); }));
  }
  // Empties the pool, to make sure all accounting
  // is done correctly.
  // unfilled_count is the number of buffers that are already reserved.
  void CheckEmptyPool(size_t unfilled_count) {
    size_t release_errors = 0;
    for (uint32_t i = 0; i < kNumVmos; ++i) {
      if (pool_.ReleaseBuffer(i) == ZX_ERR_NOT_FOUND) {
        release_errors++;
        EXPECT_LE(release_errors, unfilled_count);
      }
    }
    // Make sure we had exactly filled_count buffers already released.
    EXPECT_EQ(unfilled_count, release_errors);
    // Now, make sure all buffers are now released.
    for (uint32_t i = 0; i < kNumVmos; ++i) {
      EXPECT_EQ(pool_.ReleaseBuffer(i), ZX_ERR_NOT_FOUND);
    }
  }

  void CheckAccounting(size_t filled_count) {
    CheckFillingPool(filled_count);
    CheckEmptyPool(0);
  }

  // Shuffles the free list, psuedo-randomly.
  // Assumes that the pool is empty.
  // This shuffle function relies on the fact that if you have a prime
  // number p and a number (n) that does not have that prime number
  // as a factor, the set of (x*p)%n, where x := {0,n-1} will cover the
  // range of {0,n-1} exactly.
  void ShufflePool() {
    FillBuffers(kNumVmos);
    static constexpr uint32_t kHashingSeed = 7;
    static_assert(kNumVmos % kHashingSeed != 0, "Bad Hashing seed");
    uint32_t hashing_index = 0;
    for (size_t i = 0; i < kNumVmos; ++i) {
      hashing_index = (hashing_index + kHashingSeed) % kNumVmos;
      ASSERT_OK(pool_.ReleaseBuffer(hashing_index));
    }
  }

  zx::bti bti_;
};

TEST_F(VmoPoolTester, FillAndEmptyPool) {
  Init();
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, FillAndEmptyPinnedPool) {
  InitContiguous();
  CheckAccounting(0);
  PinVmos(fzl::VmoPool::RequireContig::Yes, fzl::VmoPool::RequireLowMem::Yes);
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, FillAndEmptyMappedPool) {
  Init();
  CheckAccounting(0);
  MapVmos();
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, NoncontigPinnedPool) {
  Init();
  PinVmos(fzl::VmoPool::RequireContig::No, fzl::VmoPool::RequireLowMem::Yes);
}

TEST_F(VmoPoolTester, DoubleGetBuffer) {
  Init();
  auto buffer = pool_.LockBufferForWrite();
  EXPECT_TRUE(buffer.has_value());
  auto buffer2 = pool_.LockBufferForWrite();
  EXPECT_TRUE(buffer.has_value());

  // Now check accounting:
  CheckAccounting(2);
}

// Checks that you can cancel a buffer, which will put it back into the pool.
TEST_F(VmoPoolTester, ReleaseBeforeComplete) {
  Init();
  auto buffer = pool_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());
  EXPECT_OK(buffer->Release());

  // Now check accounting:
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, ReleaseWrongBuffer) {
  Init();

  auto buffer = pool_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());
  ASSERT_TRUE(buffer->valid());
  uint32_t current_buffer = buffer->ReleaseWriteLockAndGetIndex();
  // Make sure that we can't mark complete twice:
  ASSERT_DEATH(([&buffer]() { buffer->ReleaseWriteLockAndGetIndex(); }));
  // Test an out-of-bounds index:
  EXPECT_EQ(pool_.ReleaseBuffer(kNumVmos), ZX_ERR_INVALID_ARGS);
  // Test all of the buffer indices that are not locked:
  for (uint32_t i = 0; i < kNumVmos; ++i) {
    if (i == current_buffer) {
      continue;
    }
    EXPECT_EQ(pool_.ReleaseBuffer(i), ZX_ERR_NOT_FOUND);
  }
  // Now check accounting:
  CheckAccounting(1);
}

// Checks that the pool does not need to be amptied or filled in any particular
// order.
TEST_F(VmoPoolTester, OutOfOrder) {
  Init();
  ShufflePool();
  // Now check accounting:
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, Reset) {
  Init();
  size_t test_cases[]{0, 1, kNumVmos / 2, kNumVmos};
  for (size_t buffer_count : test_cases) {
    // With no buffer in progress
    FillBuffers(buffer_count);
    pool_.Reset();
    CheckAccounting(0);
  }
}

TEST_F(VmoPoolTester, Reinit) {
  Init();
  CheckAccounting(0);

  Init();
  CheckAccounting(0);
}

TEST_F(VmoPoolTester, StdMove) {
  Init();
  fzl::VmoPool::Buffer source;
  fzl::VmoPool::Buffer destination;

  destination = std::move(source);

  CheckAccounting(0);
}

}  // namespace
