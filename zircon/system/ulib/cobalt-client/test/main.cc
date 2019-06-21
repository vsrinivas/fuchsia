// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zxtest/c/zxtest.h>

int main(int argc, char** argv) {
    const bool success = unittest_run_all_tests(argc, argv);
    if (!success) {
        return EXIT_FAILURE;
    }

    const bool zxtest_success = RUN_ALL_TESTS(argc, argv) == 0;
    if (!zxtest_success) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
