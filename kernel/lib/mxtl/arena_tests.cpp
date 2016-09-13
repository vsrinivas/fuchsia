// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <unittest.h>

#include <mxtl/arena.h>

static int arena_dtor_count;

struct ArenaFoo {
  char ff;
  int xx, yy, zz;

  ArenaFoo(int x, int y, int z) : ff(0), xx(x), yy(y), zz(z) {}
  ~ArenaFoo() { ++arena_dtor_count; }
};


static bool arena_test(void* context)
{
    arena_dtor_count = 0;
    BEGIN_TEST;
    mxtl::TypedArena<ArenaFoo> arena;
    arena.Init("arena_tests", 1000);

    const int count = 30;

    for (int times = 0; times != 5; ++times) {
        ArenaFoo* afp[count] = {0};

        for (int ix = 0; ix != count; ++ix) {
            afp[ix] = arena.New(17, 5, ix + 100);
            EXPECT_TRUE(afp[ix] != nullptr, "");
        }

        arena.Delete(afp[3]);
        arena.Delete(afp[4]);
        arena.Delete(afp[5]);
        afp[3] = afp[4] = afp[5] = nullptr;

        afp[4] = arena.New(17, 5, 104);

        for (int ix = 0; ix != count; ++ix) {
            if (!afp[ix]) continue;

            EXPECT_EQ(17, afp[ix]->xx, "");
            EXPECT_EQ(5, afp[ix]->yy, "");
            EXPECT_EQ(ix + 100, afp[ix]->zz, "");

            arena.Delete(afp[ix]);
        }

        EXPECT_EQ((count + 1) * (times + 1), arena_dtor_count, "");

        // Leak a few objects.
        for (int ix = 0; ix != 7; ++ix) {
          auto leak = arena.New(2121, 77, 55);
          EXPECT_TRUE(leak != nullptr, "");
        }
    }
    END_TEST;
}

UNITTEST_START_TESTCASE(arena_tests)
UNITTEST("Arena allocator test", arena_test)
UNITTEST_END_TESTCASE(arena_tests, "arenatests", "Arena allocator test", NULL, NULL);
