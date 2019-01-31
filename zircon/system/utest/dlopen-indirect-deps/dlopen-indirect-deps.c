// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dlfcn.h>

#include <unittest/unittest.h>

bool dlopen_indirect_deps_test(void) {
    BEGIN_TEST;

    void* h = dlopen("libdlopen-indirect-deps-test-module.so", RTLD_LOCAL);
    ASSERT_NONNULL(h, dlerror());

    EXPECT_NONNULL(dlsym(h, "module_symbol"),
                   "symbol not found in dlopen'd lib");

    EXPECT_NONNULL(dlsym(h, "liba_symbol"),
                   "symbol not found in dlopen'd lib's direct dependency");

    EXPECT_NONNULL(dlsym(h, "libb_symbol"),
                   "symbol not found in dlopen'd lib's indirect dependency");

    EXPECT_EQ(dlclose(h), 0, "dlclose failed");

    END_TEST;
}

BEGIN_TEST_CASE(dlopen_indirect_deps_tests)
RUN_TEST(dlopen_indirect_deps_test);
END_TEST_CASE(dlopen_indirect_deps_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
