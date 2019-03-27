// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <id_allocator/id_allocator.h>
#include <unittest/unittest.h>

namespace id_allocator {
namespace {

// Allocate and check if the id is in fact busy
bool AllocHelper(std::unique_ptr<IdAllocator>& ida, size_t* ret_id, const char* cmsg) {
    size_t id;
    char msg[80];

    BEGIN_HELPER;
    snprintf(msg, 80, "%s alloc failed", cmsg);
    ASSERT_EQ(ida->Allocate(&id), ZX_OK, msg);

    snprintf(msg, 80, "%s busy failed", cmsg);
    ASSERT_TRUE(ida->IsBusy(id), msg);

    *ret_id = id;
    END_HELPER;
}

// MarkAllocated a given id and check if the id is in fact busy
bool MarkAllocatedHelper(std::unique_ptr<IdAllocator>& ida, size_t id, const char* cmsg) {
    char msg[80];

    BEGIN_HELPER;
    snprintf(msg, 80, "%s avail failed", cmsg);
    ASSERT_FALSE(ida->IsBusy(id), msg);

    snprintf(msg, 80, "%s MarkAllocated failed", cmsg);
    ASSERT_EQ(ida->MarkAllocated(id), ZX_OK, msg);

    snprintf(msg, 80, "%s busy failed", cmsg);
    ASSERT_TRUE(ida->IsBusy(id), msg);
    ida->Dump();

    END_HELPER;
}

// Free a given id and verify
bool FreeHelper(std::unique_ptr<IdAllocator>& ida, size_t id, const char* cmsg) {
    char msg[80];

    BEGIN_HELPER;
    snprintf(msg, 80, "%s busy failed", cmsg);
    ASSERT_TRUE(ida->IsBusy(id), msg);

    snprintf(msg, 80, "%s free failed", cmsg);
    ASSERT_EQ(ida->Free(id), ZX_OK, msg);

    snprintf(msg, 80, "%s avail failed", cmsg);
    ASSERT_FALSE(ida->IsBusy(id), msg);

    END_HELPER;
}

// Try to use 0 initialized resource
bool TestInitializedEmpty() {
    BEGIN_TEST;

    size_t id;
    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(0, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 0U, "get size failed");

    ASSERT_FALSE(ida->IsBusy(0), "get one bit failed");
    ASSERT_NE(ida->Allocate(&id), ZX_OK, "set one bit failed");
    ASSERT_EQ(ida->Free(0), ZX_ERR_OUT_OF_RANGE, "clear one bit failed");

    ida = nullptr;
    ASSERT_EQ(IdAllocator::Create(1, &ida), ZX_OK);

    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(FreeHelper(ida, id, __func__), true);
    END_TEST;
}

// Simple allocate and free test
bool TestSingleAlloc() {
    BEGIN_TEST;

    size_t id;
    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 128U, "get size failed");

    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(FreeHelper(ida, id, __func__), true);

    END_TEST;
}

// Simple MarkAllocated and free test
bool TestSingleMarkAllocated() {
    BEGIN_TEST;

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 128U, "get size failed");

    ASSERT_EQ(MarkAllocatedHelper(ida, 2, __func__), true);
    ASSERT_EQ(FreeHelper(ida, 2, __func__), true);

    END_TEST;
}

// Try to MarkAllocated id twice. Check if second attempt returns
// a non-OK status
bool TestMarkAllocatedTwice() {
    BEGIN_TEST;

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 128U, "get size failed");

    ASSERT_EQ(MarkAllocatedHelper(ida, 2, __func__), true);

    ASSERT_NE(ida->MarkAllocated(2), ZX_OK, "set bit again failed");
    ASSERT_EQ(FreeHelper(ida, 2, __func__), true);

    END_TEST;
}

// Try to free an allocated id twice. Check if second attempt returns
// a non-OK status
bool TestFreeTwice() {
    BEGIN_TEST;

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 128U, "get size failed");

    ASSERT_EQ(MarkAllocatedHelper(ida, 2, __func__), true);
    ASSERT_EQ(FreeHelper(ida, 2, __func__), true);
    ASSERT_NE(ida->Free(2), ZX_OK, " second free failed");
    ASSERT_FALSE(ida->IsBusy(2), " busy check failed");

    END_TEST;
}

// Tests interleaved alloc with MarkAllocated.
bool TestAllocInterleaved() {
    BEGIN_TEST;

    std::unique_ptr<IdAllocator> ida;
    size_t id;
    ASSERT_EQ(IdAllocator::Create(128, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), 128U, "get size failed");

    ASSERT_FALSE(ida->IsBusy(2), "get bit with null");

    ASSERT_EQ(MarkAllocatedHelper(ida, 2, __func__), true);
    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(id, 0, " alloc0 failed");
    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(id, 1, " alloc1 failed");
    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(id, 3, " alloc3 failed");

    ASSERT_EQ(MarkAllocatedHelper(ida, 4, "MarkAllocated4 failed"), true);
    ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
    ASSERT_EQ(id, 5, " alloc5 failed");

    END_TEST;
}

// Cross check if allocator size matches the given size and
// allocate all ids
bool AllocAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
    BEGIN_HELPER;
    ASSERT_EQ(ida->Size(), size, "get size failed");

    size_t id;

    for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(AllocHelper(ida, &id, __func__), true);
        ASSERT_EQ(id, i, " alloc all failed");
    }

    ASSERT_NE(ida->Allocate(&id), ZX_OK, "alloc all one more failed");

    END_HELPER;
}

// Cross check if allocator size matches the given size and
// free all ids
bool FreeAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
    BEGIN_HELPER;
    ASSERT_EQ(ida->Size(), size, "get size failed");

    for (size_t i = 0; i < size; i++) {
        ASSERT_EQ(FreeHelper(ida, i, __func__), true);
    }

    END_HELPER;
}

// Cross check if allocator size matches the given size,
// allocate and then free all ids
bool AllocFreeAllHelper(std::unique_ptr<IdAllocator>& ida, size_t size) {
    BEGIN_HELPER;
    ASSERT_EQ(AllocAllHelper(ida, size), true);
    ASSERT_EQ(FreeAllHelper(ida, size), true);
    END_HELPER;
}

// Test allocating and then freeing all the ids for a given size
// of allocator. Ensures that all freed ids are reallocatable
template <size_t Size>
bool TestAllocAll() {
    BEGIN_TEST;

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(Size, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), Size, "get size failed");

    ASSERT_EQ(AllocFreeAllHelper(ida, Size), true);
    ASSERT_EQ(AllocAllHelper(ida, Size), true);

    END_TEST;
}

// Test allocating and then resetting all the ids for a given
// size of allocator. Ensures that after reset all ids are
// free and are reallocatable.
template <size_t InputSize>
bool TestReset() {
    BEGIN_TEST;
    size_t size = InputSize;

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(size, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), size, "get size failed");

    ASSERT_EQ(AllocAllHelper(ida, size), true);

    // Reset() with same size should free all ids
    ASSERT_EQ(ida->Reset(size), ZX_OK);
    ASSERT_EQ(ida->Size(), size, "get size failed");
    ASSERT_EQ(AllocFreeAllHelper(ida, size), true);

    ASSERT_EQ(AllocAllHelper(ida, size), true);
    size = size / 2;
    // Reset() with smaller size should shrink and then free all ids
    ASSERT_EQ(ida->Reset(size), ZX_OK);
    ASSERT_EQ(ida->Size(), size, "get size failed");
    ASSERT_EQ(AllocFreeAllHelper(ida, size), true);

    ASSERT_EQ(AllocAllHelper(ida, size), true);
    // Reset() with larger size should grow and then free all ids
    size = size * 3;
    ASSERT_EQ(ida->Reset(size), ZX_OK);
    ASSERT_EQ(ida->Size(), size, "get size failed");
    ASSERT_EQ(AllocFreeAllHelper(ida, size), true);

    size = 0;
    ASSERT_EQ(ida->Reset(size), ZX_OK);
    ASSERT_EQ(ida->Size(), size, "get size failed");
    ASSERT_EQ(AllocFreeAllHelper(ida, size), true);

    END_TEST;
}

// MarkAllocatedate ids at start, mid and end of a given range. This will help
// us to validate that we haven't corrupted id states during Grow.
bool BoundaryMarkAllocated(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
    BEGIN_HELPER;
    ASSERT_EQ(MarkAllocatedHelper(ida, start, "start MarkAllocated"), true);
    ASSERT_EQ(MarkAllocatedHelper(ida, (start + end) / 2, "mid MarkAllocated"),
              true);
    ASSERT_EQ(MarkAllocatedHelper(ida, end, "end MarkAllocated"), true);
    END_HELPER;
}

// Check ids at start, mid and end of a given range stay allocated.
// This will help us to validate that we haven't corrupted id states
// during Grow.
bool BoundaryCheck(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
    BEGIN_HELPER;
    ASSERT_TRUE(ida->IsBusy(start), "start check");
    ASSERT_TRUE(ida->IsBusy((start + end) / 2), "mid check");
    ASSERT_TRUE(ida->IsBusy(end), "end check");
    END_HELPER;
}

// Free ids at start, mid and end of a given range stay allocated.
bool BoundaryFree(std::unique_ptr<IdAllocator>& ida, size_t start, size_t end) {
    BEGIN_HELPER;
    ASSERT_TRUE(FreeHelper(ida, start, "start free"));
    ASSERT_TRUE(FreeHelper(ida, (start + end) / 2, "mid free"));
    ASSERT_TRUE(FreeHelper(ida, end, "end free"));
    END_HELPER;
}

// Grow [and reset] an allocator. Check if
// 1. allocated ids stay allocated after grow.
// 2. We can allocated all the ids
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size, bool Reset>
bool TestGrowReset() {
    BEGIN_TEST;
    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(InitSize, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), InitSize, "get size");

    // Allocate "few" ids before we grow an allocator
    ASSERT_EQ(BoundaryMarkAllocated(ida, 0, InitSize - 1), true);
    if (Reset) {
        ASSERT_EQ(ida->Reset(Phase1Size), ZX_OK, "reset phase1 failed");
    } else {
        ASSERT_EQ(ida->Grow(Phase1Size), ZX_OK, "grow phase1 failed");
        // Check if the allocated ids before Grow are still allocated
        ASSERT_EQ(BoundaryCheck(ida, 0, InitSize - 1), true);
        // Free allocated ids
        ASSERT_EQ(BoundaryFree(ida, 0, InitSize - 1), true);
    }
    // Check if Grow really worked by allocating and freeing all ids
    ASSERT_EQ(AllocFreeAllHelper(ida, Phase1Size), true);

    // Rinse and repeat. The following block ensures that we can grow
    // from phase1 to phase2 with before and after size aligned and
    // unaligned to 64.
    ASSERT_EQ(BoundaryMarkAllocated(ida, InitSize, Phase1Size - 1), true);
    if (Reset) {
        ASSERT_EQ(ida->Reset(Phase2Size), ZX_OK, "reset phase2 failed");
    } else {
        ASSERT_EQ(ida->Grow(Phase2Size), ZX_OK, "grow phase2 failed");
        ASSERT_EQ(BoundaryCheck(ida, InitSize, Phase1Size - 1), true);
        ASSERT_EQ(BoundaryFree(ida, InitSize, Phase1Size - 1), true);
    }
    ASSERT_EQ(AllocFreeAllHelper(ida, Phase2Size), true);

    END_TEST;
}

template <size_t StepSize, size_t MaxId>
bool TestGrowSteps() {
    BEGIN_TEST;
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
    END_TEST;
}

// Shrink [and reset] an allocator. Check if
// 1. allocated ids stay allocated after Shrink.
// 2. We can allocated all the ids
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size, bool Reset>
bool TestShrinkReset() {
    BEGIN_TEST;

    ASSERT_GT(InitSize, Phase1Size);
    ASSERT_GT(Phase1Size, Phase2Size);

    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(InitSize, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), InitSize, "get size");

    // Allocate "few" ids before we shrink an allocator
    ASSERT_EQ(BoundaryMarkAllocated(ida, 0, Phase1Size - 1), true);
    if (Reset) {
        ASSERT_EQ(ida->Reset(Phase1Size), ZX_OK, "reset phase1 failed");
    } else {
        ASSERT_EQ(ida->Shrink(Phase1Size), ZX_OK, "shrink phase1 failed");
        // Check if the allocated ids before Shrink are still allocated
        ASSERT_EQ(BoundaryCheck(ida, 0, Phase1Size - 1), true);
        // Free allocated ids
        ASSERT_EQ(BoundaryFree(ida, 0, Phase1Size - 1), true);
    }
    // Check if shrink really worked by allocating and freeing all ids
    ASSERT_EQ(AllocFreeAllHelper(ida, Phase1Size), true);

    // Rinse and repeat. The following block ensures that we can shrink
    // from phase1 to phase2 with before and after size aligned and
    // unaligned to 64.
    ASSERT_EQ(BoundaryMarkAllocated(ida, 0, Phase2Size - 1), true);
    if (Reset) {
        ASSERT_EQ(ida->Reset(Phase2Size), ZX_OK, "reset phase1 failed");
    } else {
        ASSERT_EQ(ida->Shrink(Phase2Size), ZX_OK, "shrink phase2 failed");
        ASSERT_EQ(BoundaryCheck(ida, 0, Phase2Size - 1), true);
        ASSERT_EQ(BoundaryFree(ida, 0, Phase2Size - 1), true);
    }
    ASSERT_EQ(AllocFreeAllHelper(ida, Phase2Size), true);

    END_TEST;
}

template <size_t StepSize, size_t MaxId>
bool TestShrinkSteps() {
    BEGIN_TEST;
    std::unique_ptr<IdAllocator> ida;
    ASSERT_EQ(IdAllocator::Create(MaxId, &ida), ZX_OK);
    ASSERT_EQ(ida->Size(), MaxId, "get size");
    ASSERT_EQ(AllocAllHelper(ida, MaxId), true);

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
    END_TEST;
}

// Reset is nothing but destructive grow/shrink + free all allocated ids.
// We write wrappers around Grow/Shrink tests to make results spit out what
// failed.
template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
bool TestGrow() {
    BEGIN_TEST;
    return TestGrowReset<InitSize, Phase1Size, Phase2Size, false>();
    END_TEST;
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
bool TestShrink() {
    BEGIN_TEST;
    return TestShrinkReset<InitSize, Phase1Size, Phase2Size, false>();
    END_TEST;
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
bool TestResetGrow() {
    BEGIN_TEST;
    ASSERT_TRUE((InitSize < Phase1Size) && (Phase1Size < Phase2Size));
    return TestGrowReset<InitSize, Phase1Size, Phase2Size, true>();
    END_TEST;
}

template <size_t InitSize, size_t Phase1Size, size_t Phase2Size>
bool TestResetShrink() {
    BEGIN_TEST;
    ASSERT_TRUE((InitSize > Phase1Size) && (Phase1Size > Phase2Size));
    return TestShrinkReset<InitSize, Phase1Size, Phase2Size, true>();
    END_TEST;
}

// IdAllocator uses 64-bit alignment for performance. Levels contains bits
// that are rounded up to 64-bits. Each parent can have at most 64 children.
// If a parent has less than 64 children, 64 children are allocated and
// all unallocatable children are marked busy. These forced alignments may
// introduce unexpected results/bugs. So we try to test, specifically Grow
// and AllocAll, the allocator around these boundary values
BEGIN_TEST_CASE(IdAllocatorTests)
RUN_TEST_SMALL((TestInitializedEmpty))
RUN_TEST_SMALL((TestSingleAlloc))
RUN_TEST_SMALL((TestSingleMarkAllocated))
RUN_TEST_SMALL((TestMarkAllocatedTwice))
RUN_TEST_SMALL((TestFreeTwice))
RUN_TEST_SMALL((TestAllocInterleaved))
RUN_TEST_SMALL((TestAllocAll<51>))
RUN_TEST_SMALL((TestAllocAll<64>))
RUN_TEST_SMALL((TestAllocAll<64 * 63>))
RUN_TEST_SMALL((TestAllocAll<64 * 64>))
RUN_TEST_SMALL((TestAllocAll<2 * 64 * 64>))

RUN_TEST_SMALL((TestGrow<5, 11, 63>))
RUN_TEST_SMALL((TestGrow<6, 37, 64>))
RUN_TEST_SMALL((TestGrow<16, 64, 101>))
RUN_TEST_SMALL((TestGrow<32, 64, 128>))           // Two levels
RUN_TEST_MEDIUM((TestGrow<32, 64 * 64, 1 << 20>)) // Million ids. 4 levels.
RUN_TEST_MEDIUM((TestGrowSteps<1, 1 << 15>))      // Grow 1 id at a time
RUN_TEST_MEDIUM((TestGrowSteps<128, 1 << 20>))    // Grow few ids at a time

RUN_TEST_SMALL((TestShrink<63, 11, 5>))
RUN_TEST_SMALL((TestShrink<64, 37, 6>))
RUN_TEST_SMALL((TestShrink<101, 64, 16>))
RUN_TEST_SMALL((TestShrink<128, 64, 32>))           // Two levels
RUN_TEST_MEDIUM((TestShrink<1 << 20, 64 * 64, 35>)) // Million ids. 4 levels.
RUN_TEST_MEDIUM((TestShrinkSteps<1, 1 << 15>))      // Grow 1 id at a time
RUN_TEST_MEDIUM((TestShrinkSteps<128, 1 << 20>))    // Grow few ids at a time

RUN_TEST_SMALL((TestReset<51>))
RUN_TEST_SMALL((TestReset<64>))
RUN_TEST_SMALL((TestReset<64 * 63>))
RUN_TEST_SMALL((TestReset<64 * 64>))

RUN_TEST_SMALL((TestResetGrow<5, 11, 63>))
RUN_TEST_SMALL((TestResetGrow<6, 37, 64>))
RUN_TEST_SMALL((TestResetGrow<16, 64, 101>))
RUN_TEST_SMALL((TestResetGrow<32, 64, 128>))           // Two levels
RUN_TEST_MEDIUM((TestResetGrow<32, 64 * 64, 1 << 20>)) // Million ids. 4 levels.

RUN_TEST_SMALL((TestResetShrink<63, 11, 5>))
RUN_TEST_SMALL((TestResetShrink<64, 37, 6>))
RUN_TEST_SMALL((TestResetShrink<101, 64, 16>))
RUN_TEST_SMALL((TestResetShrink<128, 64, 32>))           // Two levels
RUN_TEST_MEDIUM((TestResetShrink<1 << 20, 64 * 64, 35>)) // Million ids. 4 levels.

END_TEST_CASE(IdAllocatorTests)

} // namespace
} // namespace id_allocator
