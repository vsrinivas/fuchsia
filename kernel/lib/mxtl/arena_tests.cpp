// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <unittest.h>

#include <mxtl/arena.h>

struct ArenaFoo {
    int xx, yy, zz;
};

static bool arena_test(void* context) {
    BEGIN_TEST;
    mxtl::Arena arena;
    status_t s = arena.Init("arena_tests", sizeof(ArenaFoo), 1000);
    REQUIRE_EQ(NO_ERROR, s, "arena.Init()");

    const int count = 30;

    for (int times = 0; times != 5; ++times) {
        ArenaFoo* afp[count] = {0};

        for (int ix = 0; ix != count; ++ix) {
            afp[ix] = reinterpret_cast<ArenaFoo*>(arena.Alloc());
            REQUIRE_NONNULL(afp[ix], "arena.Alloc()");
            *afp[ix] = {17, 5, ix + 100};
        }

        arena.Free(afp[3]);
        arena.Free(afp[4]);
        arena.Free(afp[5]);
        afp[3] = afp[4] = afp[5] = nullptr;

        afp[4] = reinterpret_cast<ArenaFoo*>(arena.Alloc());
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
            ArenaFoo* leak = reinterpret_cast<ArenaFoo*>(arena.Alloc());
            REQUIRE_NONNULL(leak, "arena.Alloc()");
            *leak = {2121, 77, 55};
        }
    }
    END_TEST;
}

UNITTEST_START_TESTCASE(arena_tests)
UNITTEST("Arena allocator test", arena_test)
UNITTEST_END_TESTCASE(arena_tests, "arenatests", "Arena allocator test", nullptr, nullptr);
