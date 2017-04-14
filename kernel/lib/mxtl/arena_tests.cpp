// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <mxtl/arena.h>

#include <app/tests.h>
#include <kernel/vm/vm_aspace.h>
#include <unittest.h>

using mxtl::Arena;

struct TestObj {
    int xx, yy, zz;
};

static bool init_zero_ob_size_fails(void* context) {
    BEGIN_TEST;
    Arena arena;
    EXPECT_EQ(ERR_INVALID_ARGS, arena.Init("name", 0, 16), "");
    END_TEST;
}

static bool init_large_ob_size_fails(void* context) {
    BEGIN_TEST;
    Arena arena;
    EXPECT_EQ(ERR_INVALID_ARGS, arena.Init("name", PAGE_SIZE + 1, 16), "");
    END_TEST;
}

static bool init_zero_count_fails(void* context) {
    BEGIN_TEST;
    Arena arena;
    EXPECT_EQ(ERR_INVALID_ARGS, arena.Init("name", sizeof(TestObj), 0), "");
    END_TEST;
}

static bool start_and_end_look_good(void* context) {
    BEGIN_TEST;
    static const size_t num_slots = (2 * PAGE_SIZE) / sizeof(TestObj);
    static const size_t expected_size = num_slots * sizeof(TestObj);

    Arena arena;
    EXPECT_EQ(NO_ERROR, arena.Init("name", sizeof(TestObj), num_slots), "");

    EXPECT_NONNULL(arena.start(), "");
    EXPECT_NONNULL(arena.end(), "");
    auto start = reinterpret_cast<vaddr_t>(arena.start());
    auto end = reinterpret_cast<vaddr_t>(arena.end());
    EXPECT_LT(start, end, "");
    EXPECT_GE(end - start, expected_size, "");
    END_TEST;
}

static bool in_range_tests(void* context) {
    BEGIN_TEST;
    static const size_t num_slots = (2 * PAGE_SIZE) / sizeof(TestObj);

    Arena arena;
    EXPECT_EQ(NO_ERROR, arena.Init("name", sizeof(TestObj), num_slots), "");

    auto start = reinterpret_cast<char*>(arena.start());

    // Nothing is allocated yet, so not even the start address
    // should be in range.
    EXPECT_FALSE(arena.in_range(start), "");

    // Allocate some objects, and check that each is within range.
    static const int nobjs = 16;
    void* objs[nobjs];
    for (int i = 0; i < nobjs; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%d]", i);
        objs[i] = arena.Alloc();
        EXPECT_NONNULL(objs[i], msg);
        // The allocated object should be in range.
        EXPECT_TRUE(arena.in_range(objs[i]), msg);
        // The slot just after this object should not be in range.
        // FRAGILE: assumes that objects are allocated in increasing order.
        EXPECT_FALSE(
            arena.in_range(reinterpret_cast<TestObj*>(objs[i]) + 1), msg);
    }

    // Deallocate the objects and check whether they're in range.
    for (int i = nobjs - 1; i >= 0; i--) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%d]", i);
        // The object should still be in range.
        EXPECT_TRUE(arena.in_range(objs[i]), msg);

        arena.Free(objs[i]);

        // The free slot will still be in range.
        // NOTE: If Arena ever learns to coalesce and decommit whole pages of
        // free objects, this test will need to change.
        EXPECT_TRUE(arena.in_range(objs[i]), msg);
    }
    END_TEST;
}

static bool out_of_memory(void* context) {
    BEGIN_TEST;
    static const size_t num_slots = (2 * PAGE_SIZE) / sizeof(TestObj);

    Arena arena;
    EXPECT_EQ(NO_ERROR, arena.Init("name", sizeof(TestObj), num_slots), "");

    // Allocate all of the data objects.
    void** objs = reinterpret_cast<void**>(malloc(sizeof(void*) * num_slots));
    void** top = objs;
    for (size_t i = 0; i < num_slots; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%zu]", i);
        *top++ = arena.Alloc();
        EXPECT_NONNULL(top[-1], msg);
    }

    // Any further allocations should return nullptr.
    EXPECT_NULL(arena.Alloc(), "");
    EXPECT_NULL(arena.Alloc(), "");
    EXPECT_NULL(arena.Alloc(), "");
    EXPECT_NULL(arena.Alloc(), "");

    // Free two objects.
    arena.Free(*--top);
    arena.Free(*--top);

    // Two allocations should succeed; any further should fail.
    *top++ = arena.Alloc();
    EXPECT_NONNULL(top[-1], "");
    *top++ = arena.Alloc();
    EXPECT_NONNULL(top[-1], "");
    EXPECT_NULL(arena.Alloc(), "");
    EXPECT_NULL(arena.Alloc(), "");

    // Free all objects.
    // Nothing much to check except that it doesn't crash.
    while (top > objs) {
        arena.Free(*--top);
    }

    free(objs);
    END_TEST;
}

// Test helper. Counts the number of committed and uncommitted pages in the
// range. Returns {*committed, *uncommitted} = {0, 0} if |start| doesn't
// correspond to a live VmMapping.
static bool count_committed_pages(
    vaddr_t start, vaddr_t end, size_t* committed, size_t* uncommitted) {
    BEGIN_TEST; // Not a test, but we need these guards to use REQUIRE_*
    *committed = 0;
    *uncommitted = 0;

    // Find the VmMapping that covers |start|. Assume that it covers |end-1|.
    const auto region = VmAspace::kernel_aspace()->FindRegion(start);
    REQUIRE_NONNULL(region, "FindRegion");
    const auto mapping = region->as_vm_mapping();
    if (mapping == nullptr) {
        // It's a VMAR, not a mapping, so no pages are committed.
        // Return 0/0.
        return true;
    }

    // Ask the VMO how many pages it's allocated within the range.
    auto start_off = ROUNDDOWN(start, PAGE_SIZE) - mapping->base();
    auto end_off = ROUNDUP(end, PAGE_SIZE) - mapping->base();
    *committed = mapping->vmo()->AllocatedPagesInRange(
        start_off + mapping->object_offset(), end_off - start_off);
    *uncommitted = (end_off - start_off) / PAGE_SIZE - *committed;
    END_TEST;
}

// Checks that destroying an arena unmaps all of its pages.
static bool memory_cleanup(void* context) {
    BEGIN_TEST;
    static const size_t num_slots = (16 * PAGE_SIZE) / sizeof(TestObj);

    AllocChecker ac;
    Arena* arena = new (&ac) Arena();
    EXPECT_TRUE(ac.check(), "");
    EXPECT_EQ(NO_ERROR, arena->Init("name", sizeof(TestObj), num_slots), "");

    auto start = reinterpret_cast<vaddr_t>(arena->start());
    auto end = reinterpret_cast<vaddr_t>(arena->end());

    // Allocate, touch, and leak a bunch of objects.
    for (size_t i = 0; i < num_slots; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "[%zu]", i);
        TestObj* obj = reinterpret_cast<TestObj*>(arena->Alloc());
        EXPECT_NONNULL(obj, msg);
        *obj = {};
    }

    // Should see some committed pages.
    size_t committed;
    size_t uncommitted;
    EXPECT_TRUE(
        count_committed_pages(start, end, &committed, &uncommitted), "");
    EXPECT_GT(committed, 0u, "");

    // Destroying the Arena should destroy the underlying VmMapping,
    // along with all of its pages.
    delete arena;
    EXPECT_TRUE(
        count_committed_pages(start, end, &committed, &uncommitted), "");
    // 0/0 means "no mapping at this address".
    // FLAKY: Another thread could could come in and allocate a mapping at the
    // old location.
    EXPECT_EQ(committed, 0u, "");
    EXPECT_EQ(uncommitted, 0u, "");
    END_TEST;
}

// Basic checks that the contents of allocated objects stick around, aren't
// stomped on.
static bool content_preservation(void* context) {
    BEGIN_TEST;
    Arena arena;
    status_t s = arena.Init("arena_tests", sizeof(TestObj), 1000);
    REQUIRE_EQ(NO_ERROR, s, "arena.Init()");

    const int count = 30;

    for (int times = 0; times != 5; ++times) {
        TestObj* afp[count] = {0};

        for (int ix = 0; ix != count; ++ix) {
            afp[ix] = reinterpret_cast<TestObj*>(arena.Alloc());
            REQUIRE_NONNULL(afp[ix], "arena.Alloc()");
            *afp[ix] = {17, 5, ix + 100};
        }

        arena.Free(afp[3]);
        arena.Free(afp[4]);
        arena.Free(afp[5]);
        afp[3] = afp[4] = afp[5] = nullptr;

        afp[4] = reinterpret_cast<TestObj*>(arena.Alloc());
        REQUIRE_NONNULL(afp[4], "arena.Alloc()");
        *afp[4] = {17, 5, 104};

        for (int ix = 0; ix != count; ++ix) {
            if (!afp[ix])
                continue;

            EXPECT_EQ(17, afp[ix]->xx, "");
            EXPECT_EQ(5, afp[ix]->yy, "");
            EXPECT_EQ(ix + 100, afp[ix]->zz, "");

            arena.Free(afp[ix]);
        }

        // Leak a few objects.
        for (int ix = 0; ix != 7; ++ix) {
            TestObj* leak = reinterpret_cast<TestObj*>(arena.Alloc());
            REQUIRE_NONNULL(leak, "arena.Alloc()");
            *leak = {2121, 77, 55};
        }
    }
    END_TEST;
}

#define ARENA_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(arena_tests)
ARENA_UNITTEST(init_zero_ob_size_fails)
ARENA_UNITTEST(init_large_ob_size_fails)
ARENA_UNITTEST(init_zero_count_fails)
ARENA_UNITTEST(start_and_end_look_good)
ARENA_UNITTEST(in_range_tests)
ARENA_UNITTEST(out_of_memory)
ARENA_UNITTEST(memory_cleanup)
ARENA_UNITTEST(content_preservation)
UNITTEST_END_TESTCASE(arena_tests, "arenatests", "Arena allocator test", nullptr, nullptr);
