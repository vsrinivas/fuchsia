// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <id_allocator/id_allocator.h>
#include <zxtest/zxtest.h>

namespace id_allocator {
namespace {

// Allocate and check if the id is in fact busy
void AllocHelper(std::unique_ptr<IdAllocator>& ida, size_t* ret_id, const char* cmsg) {
  size_t id;
  ASSERT_EQ(ida->Allocate(&id), ZX_OK, "%s alloc failed", cmsg);
  ASSERT_TRUE(ida->IsBusy(id), "%s busy failed", cmsg);
  *ret_id = id;
}

// MarkAllocated a given id and check if the id is in fact busy
void MarkAllocatedHelper(std::unique_ptr<IdAllocator>& ida, size_t id, const char* cmsg) {
  ASSERT_FALSE(ida->IsBusy(id), "%s avail failed", cmsg);
  ASSERT_EQ(ida->MarkAllocated(id), ZX_OK, "%s MarkAllocated failed", cmsg);
  ASSERT_TRUE(ida->IsBusy(id), "%s busy failed", cmsg);
  ida->Dump();
}

// Free a given id and verify
void FreeHelper(std::unique_ptr<IdAllocator>& ida, size_t id, const char* cmsg) {
  ASSERT_TRUE(ida->IsBusy(id), "%s busy failed", cmsg);
  ASSERT_EQ(ida->Free(id), ZX_OK, "%s free failed", cmsg);
  ASSERT_FALSE(ida->IsBusy(id), "%s avail failed", cmsg);
}

// Try to use 0 initialized resource
TEST(IdAllocatorTests, TestInitializedEmpty) {
  size_t id;
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(0, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 0U, "get size failed");

  ASSERT_FALSE(ida->IsBusy(0), "get one bit failed");
  ASSERT_NE(ida->Allocate(&id), ZX_OK, "set one bit failed");
  ASSERT_EQ(ida->Free(0), ZX_ERR_OUT_OF_RANGE, "clear one bit failed");

  ida = nullptr;
  ASSERT_EQ(IdAllocator::Create(1, &ida), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, id, __func__));
}

// Simple allocate and free test
TEST(IdAllocatorTests, TestSingleAlloc) {
  size_t id;
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 128U, "get size failed");

  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, id, __func__));
}

// Simple MarkAllocated and free test
TEST(IdAllocatorTests, TestSingleMarkAllocated) {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 128U, "get size failed");

  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, 2, __func__));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, 2, __func__));
}

// Try to MarkAllocated id twice. Check if second attempt returns
// a non-OK status
TEST(IdAllocatorTests, TestMarkAllocatedTwice) {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 128U, "get size failed");

  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, 2, __func__));

  ASSERT_NE(ida->MarkAllocated(2), ZX_OK, "set bit again failed");
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, 2, __func__));
}

// Try to free an allocated id twice. Check if second attempt returns
// a non-OK status
TEST(IdAllocatorTests, TestFreeTwice) {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 128U, "get size failed");

  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, 2, __func__));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, 2, __func__));
  ASSERT_NE(ida->Free(2), ZX_OK, " second free failed");
  ASSERT_FALSE(ida->IsBusy(2), " busy check failed");
}

// Tests interleaved alloc with MarkAllocated.
TEST(IdAllocatorTests, TestAllocInterleaved) {
  std::unique_ptr<IdAllocator> ida;
  size_t id;
  ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 128U, "get size failed");

  ASSERT_FALSE(ida->IsBusy(2), "get bit with null");

  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, 2, __func__));
  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_EQ(id, 0, " alloc0 failed");
  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_EQ(id, 1, " alloc1 failed");
  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_EQ(id, 3, " alloc3 failed");

  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, 4, "MarkAllocated4 failed"));
  ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
  ASSERT_EQ(id, 5, " alloc5 failed");
}

// Cross check if allocator size matches the given size and
// allocate all ids
void AllocAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
  ASSERT_EQ(ida->Size(), size, "get size failed");

  size_t id;

  for (size_t i = 0; i < size; i++) {
    ASSERT_NO_FATAL_FAILURE(AllocHelper(ida, &id, __func__));
    ASSERT_EQ(id, i, " alloc all failed");
  }

  ASSERT_NE(ida->Allocate(&id), ZX_OK, "alloc all one more failed");
}

// Cross check if allocator size matches the given size and
// free all ids
void FreeAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
  ASSERT_EQ(ida->Size(), size, "get size failed");

  for (size_t i = 0; i < size; i++) {
    ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, i, __func__));
  }
}

// Cross check if allocator size matches the given size,
// allocate and then free all ids
void AllocFreeAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, size));
  ASSERT_NO_FATAL_FAILURE(FreeAllHelper(ida, size));
}

// Test allocating and then freeing all the ids for a given size
// of allocator. Ensures that all freed ids are reallocatable
template <size_t Size>
void TestAllocAll() {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(Size, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), Size, "get size failed");

  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, Size));
  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, Size));
}

// Test allocating and then resetting all the ids for a given
// size of allocator. Ensures that after reset all ids are
// free and are reallocatable.
template <size_t InputSize>
void TestReset() {
  size_t size = InputSize;

  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(size, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), size, "get size failed");

  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, size));

  // Reset() with same size should free all ids
  ASSERT_EQ(ida->Reset(size), ZX_OK);
  ASSERT_EQ(ida->Size(), size, "get size failed");
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, size));

  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, size));
  size = size / 2;
  // Reset() with smaller size should shrink and then free all ids
  ASSERT_EQ(ida->Reset(size), ZX_OK);
  ASSERT_EQ(ida->Size(), size, "get size failed");
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, size));

  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, size));
  // Reset() with larger size should grow and then free all ids
  size = size * 3;
  ASSERT_EQ(ida->Reset(size), ZX_OK);
  ASSERT_EQ(ida->Size(), size, "get size failed");
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, size));

  size = 0;
  ASSERT_EQ(ida->Reset(size), ZX_OK);
  ASSERT_EQ(ida->Size(), size, "get size failed");
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, size));
}

// MarkAllocatedate ids at start, mid and end of a given range. This will help
// us to validate that we haven't corrupted id states during Grow.
void BoundaryMarkAllocated(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, start, "start MarkAllocated"));
  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, (start + end) / 2, "mid MarkAllocated"));
  ASSERT_NO_FATAL_FAILURE(MarkAllocatedHelper(ida, end, "end MarkAllocated"));
}

// Check ids at start, mid and end of a given range stay allocated.
// This will help us to validate that we haven't corrupted id states
// during Grow.
void BoundaryCheck(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
  ASSERT_TRUE(ida->IsBusy(start), "start check");
  ASSERT_TRUE(ida->IsBusy((start + end) / 2), "mid check");
  ASSERT_TRUE(ida->IsBusy(end), "end check");
}

// Free ids at start, mid and end of a given range stay allocated.
void BoundaryFree(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, start, "start free"));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, (start + end) / 2, "mid free"));
  ASSERT_NO_FATAL_FAILURE(FreeHelper(ida, end, "end free"));
}

// Grow [and reset] an allocator. Check if
// 1. allocated ids stay allocated after grow.
// 2. We can allocated all the ids
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size, bool Reset>
void TestGrowReset() {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(InitSize, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), InitSize, "get size");

  // Allocate "few" ids before we grow an allocator
  ASSERT_NO_FATAL_FAILURE(BoundaryMarkAllocated(ida, 0, InitSize - 1));
  if (Reset) {
    ASSERT_EQ(ida->Reset(Phase1Size), ZX_OK, "reset phase1 failed");
  } else {
    ASSERT_EQ(ida->Grow(Phase1Size), ZX_OK, "grow phase1 failed");
    // Check if the allocated ids before Grow are still allocated
    ASSERT_NO_FATAL_FAILURE(BoundaryCheck(ida, 0, InitSize - 1));
    // Free allocated ids
    ASSERT_NO_FATAL_FAILURE(BoundaryFree(ida, 0, InitSize - 1));
  }
  // Check if Grow really worked by allocating and freeing all ids
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, Phase1Size));

  // Rinse and repeat. The following block ensures that we can grow
  // from phase1 to phase2 with before and after size aligned and
  // unaligned to 64.
  ASSERT_NO_FATAL_FAILURE(BoundaryMarkAllocated(ida, InitSize, Phase1Size - 1));
  if (Reset) {
    ASSERT_EQ(ida->Reset(Phase2Size), ZX_OK, "reset phase2 failed");
  } else {
    ASSERT_EQ(ida->Grow(Phase2Size), ZX_OK, "grow phase2 failed");
    ASSERT_NO_FATAL_FAILURE(BoundaryCheck(ida, InitSize, Phase1Size - 1));
    ASSERT_NO_FATAL_FAILURE(BoundaryFree(ida, InitSize, Phase1Size - 1));
  }
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, Phase2Size));
}

template <size_t StepSize, size_t MaxId>
void TestGrowSteps() {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(0, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), 0, "get size");
  size_t id;

  ASSERT_EQ(ida->Grow(StepSize), ZX_OK, "grow failed");
  ASSERT_EQ(ida->Size(), StepSize, "get size");
  for (size_t i = 0, step = 0; i < MaxId; i++) {
    zx_status_t ret = ida->Allocate(&id);
    EXPECT_EQ(ret, ZX_OK, "TestGrowSteps alloc failure");

    // We do not want to flood log file with failures. End test early.
    if (ret != ZX_OK) {
      ida->Dump();
      break;
    }
    ASSERT_EQ(i, id, "TestGrowSteps id alloc");
    step++;
    if (step == StepSize) {
      ASSERT_EQ(ida->Grow(i + StepSize + 1), ZX_OK, "grow failed");
      step = 0;
    }
  }
}

// Shrink [and reset] an allocator. Check if
// 1. allocated ids stay allocated after Shrink.
// 2. We can allocated all the ids
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size, bool Reset>
void TestShrinkReset() {
  ASSERT_GT(InitSize, Phase1Size);
  ASSERT_GT(Phase1Size, Phase2Size);

  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(InitSize, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), InitSize, "get size");

  // Allocate "few" ids before we shrink an allocator
  ASSERT_NO_FATAL_FAILURE(BoundaryMarkAllocated(ida, 0, Phase1Size - 1));
  if (Reset) {
    ASSERT_EQ(ida->Reset(Phase1Size), ZX_OK, "reset phase1 failed");
  } else {
    ASSERT_EQ(ida->Shrink(Phase1Size), ZX_OK, "shrink phase1 failed");
    // Check if the allocated ids before Shrink are still allocated
    ASSERT_NO_FATAL_FAILURE(BoundaryCheck(ida, 0, Phase1Size - 1));
    // Free allocated ids
    ASSERT_NO_FATAL_FAILURE(BoundaryFree(ida, 0, Phase1Size - 1));
  }
  // Check if shrink really worked by allocating and freeing all ids
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, Phase1Size));

  // Rinse and repeat. The following block ensures that we can shrink
  // from phase1 to phase2 with before and after size aligned and
  // unaligned to 64.
  ASSERT_NO_FATAL_FAILURE(BoundaryMarkAllocated(ida, 0, Phase2Size - 1));
  if (Reset) {
    ASSERT_EQ(ida->Reset(Phase2Size), ZX_OK, "reset phase1 failed");
  } else {
    ASSERT_EQ(ida->Shrink(Phase2Size), ZX_OK, "shrink phase2 failed");
    ASSERT_NO_FATAL_FAILURE(BoundaryCheck(ida, 0, Phase2Size - 1));
    ASSERT_NO_FATAL_FAILURE(BoundaryFree(ida, 0, Phase2Size - 1));
  }
  ASSERT_NO_FATAL_FAILURE(AllocFreeAllHelper(ida, Phase2Size));
}

template <size_t StepSize, size_t MaxId>
void TestShrinkSteps() {
  std::unique_ptr<IdAllocator> ida;
  ASSERT_EQ(IdAllocator::Create(MaxId, &ida), ZX_OK);
  ASSERT_EQ(ida->Size(), MaxId, "get size");
  ASSERT_NO_FATAL_FAILURE(AllocAllHelper(ida, MaxId));

  for (size_t i = MaxId - 1, step = 0; i > 0; i--) {
    zx_status_t ret = ida->Free(i);
    ASSERT_EQ(ret, ZX_OK, "free failure");

    // We do not want to flood log file with failures. End test early.
    if (ret != ZX_OK) {
      ida->Dump();
      break;
    }
    step++;
    if (step == StepSize) {
      ASSERT_EQ(ida->Shrink(i), ZX_OK, "shrink failed");
      step = 0;
    }
  }
}

// Reset is nothing but destructive grow/shrink + free all allocated ids.
// We write wrappers around Grow/Shrink tests to make results spit out what
// failed.
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
void TestGrow() {
  TestGrowReset<InitSize, Phase1Size, Phase2Size, false>();
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
void TestShrink() {
  TestShrinkReset<InitSize, Phase1Size, Phase2Size, false>();
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
void TestResetGrow() {
  ASSERT_TRUE((InitSize < Phase1Size) && (Phase1Size < Phase2Size));
  TestGrowReset<InitSize, Phase1Size, Phase2Size, true>();
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
void TestResetShrink() {
  ASSERT_TRUE((InitSize > Phase1Size) && (Phase1Size > Phase2Size));
  TestShrinkReset<InitSize, Phase1Size, Phase2Size, true>();
}

// IdAllocator uses 64-bit alignment for performance. Levels contains bits
// that are rounded up to 64-bits. Each parent can have at most 64 children.
// If a parent has less than 64 children, 64 children are allocated and
// all unallocatable children are marked busy. These forced alignments may
// introduce unexpected results/bugs. So we try to test, specifically Grow
// and AllocAll, the allocator around these boundary values
TEST(IdAllocatorTests, TestAllocAll_51) { TestAllocAll<51>(); }
TEST(IdAllocatorTests, TestAllocAll_64) { TestAllocAll<64>(); }
TEST(IdAllocatorTests, TestAllocAll_64_63) { TestAllocAll<64 * 63>(); }
TEST(IdAllocatorTests, TestAllocAll_64_64) { TestAllocAll<64 * 64>(); }
TEST(IdAllocatorTests, TestAllocAll_2_64_64) { TestAllocAll<2 * 64 * 64>(); }

TEST(IdAllocatorTests, TestGrow_5_11_63) { TestGrow<5, 11, 63>(); }
TEST(IdAllocatorTests, TestGrow_6_37_64) { TestGrow<6, 37, 64>(); }
TEST(IdAllocatorTests, TestGrow_16_64_101) { TestGrow<16, 64, 101>(); }
// Two levels
TEST(IdAllocatorTests, TestGrow_32_64_128) { TestGrow<32, 64, 128>(); }
// Million ids. 4 levels.
TEST(IdAllocatorTests, TestGrow_32_64_64_1_20) { TestGrow<32, 64 * 64, 1 << 20>(); }
// Grow 1 id at a time
TEST(IdAllocatorTests, TestGrowSteps_1_1_15) { TestGrowSteps<1, 1 << 15>(); }
// Grow few ids at a time
TEST(IdAllocatorTests, TestGrowSteps_128_1_20) { TestGrowSteps<128, 1 << 20>(); }

TEST(IdAllocatorTests, TestShrink_63_11_5) { TestShrink<63, 11, 5>(); }
TEST(IdAllocatorTests, TestShrink_64_37_6) { TestShrink<64, 37, 6>(); }
TEST(IdAllocatorTests, TestShrink_101_64_16) { TestShrink<101, 64, 16>(); }
// Two levels
TEST(IdAllocatorTests, TestShrink_128_64_32) { TestShrink<128, 64, 32>(); }
// Million ids. 4 levels.
TEST(IdAllocatorTests, TestShrink_1_20_64_64_35) { TestShrink<1 << 20, 64 * 64, 35>(); }
// Grow 1 id at a time
TEST(IdAllocatorTests, TestShrinkSteps_1_1_15) { TestShrinkSteps<1, 1 << 15>(); }
// Grow few ids at a time
TEST(IdAllocatorTests, TestShrinkSteps_128_1_20) { TestShrinkSteps<128, 1 << 20>(); }

TEST(IdAllocatorTests, TestReset_51) { TestReset<51>(); }
TEST(IdAllocatorTests, TestReset_64) { TestReset<64>(); }
TEST(IdAllocatorTests, TestReset_64_63) { TestReset<64 * 63>(); }
TEST(IdAllocatorTests, TestReset_64_64) { TestReset<64 * 64>(); }

TEST(IdAllocatorTests, TestResetGrow_5_11_63) { TestResetGrow<5, 11, 63>(); }
TEST(IdAllocatorTests, TestResetGrow_6_37_64) { TestResetGrow<6, 37, 64>(); }
TEST(IdAllocatorTests, TestResetGrow_16_64_101) { TestResetGrow<16, 64, 101>(); }
// Two levels
TEST(IdAllocatorTests, TestResetGrow_32_64_128) { TestResetGrow<32, 64, 128>(); }
// Million ids. 4 levels.
TEST(IdAllocatorTests, TestResetGrow_32_64_64_1_20) { TestResetGrow<32, 64 * 64, 1 << 20>(); }

TEST(IdAllocatorTests, TestResetShrink_63_11_5) { TestResetShrink<63, 11, 5>(); }
TEST(IdAllocatorTests, TestResetShrink_64_37_6) { TestResetShrink<64, 37, 6>(); }
TEST(IdAllocatorTests, TestResetShrink_101_64_16) { TestResetShrink<101, 64, 16>(); }
// Two levels
TEST(IdAllocatorTests, TestResetShrink_128_64_32) { TestResetShrink<128, 64, 32>(); }
// Million ids. 4 levels.
TEST(IdAllocatorTests, TestResetShrink_1_20_64_64_35) { TestResetShrink<1 << 20, 64 * 64, 35>(); }

}  // namespace
}  // namespace id_allocator
