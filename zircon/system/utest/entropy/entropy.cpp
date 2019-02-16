// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <unittest/unittest.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST

bool entropy_file_exists() {
    BEGIN_TEST;

    FILE* file = fopen("/boot/kernel/debug/entropy.bin", "rb");
    ASSERT_NONNULL(file, "entropy file doesn't exist");

    char buf[32];
    size_t read = fread(buf, 1, sizeof(buf), file);

    EXPECT_LT(0, read, "entropy file contains no data or not readable");

    END_TEST;
}

BEGIN_TEST_CASE(entropy_tests)
RUN_TEST(entropy_file_exists);
END_TEST_CASE(entropy_tests)

#endif

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
