// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-test-utils/fixture.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <unittest/unittest.h>

int main(int argc, char** argv) {
    return fs_test_utils::RunWithMemFs([argc, argv]() {
        return unittest_run_all_tests(argc, argv) ? 0 : -1;
    });
}
