// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fs-management/ramdisk.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <unittest/unittest.h>

namespace {

// This is a simple test of biotime (a block device IO performance
// measurement tool).  It runs biotime on a ramdisk and just checks that it
// returns a success status.
bool run_biotime(const char* option_arg) {
    char ramdisk_path[PATH_MAX];
    ASSERT_EQ(create_ramdisk(1024, 100, ramdisk_path), 0);
    auto ac = fbl::MakeAutoCall([&] {
        EXPECT_EQ(destroy_ramdisk(ramdisk_path), 0);
    });

    const char* argv[] = { "/boot/bin/biotime", option_arg, ramdisk_path,
                           nullptr };
    zx::process process;
    ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, argv[0], argv,
                         process.reset_and_get_address()), ZX_OK);

    // Wait for the process to exit.
    ASSERT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(),
                               nullptr), ZX_OK);
    zx_info_process_t proc_info;
    ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                               nullptr, nullptr), ZX_OK);
    ASSERT_EQ(proc_info.return_code, 0);
    return true;
}

bool test_biotime_linear_access() {
    BEGIN_TEST;
    EXPECT_TRUE(run_biotime("-linear"));
    END_TEST;
}

bool test_biotime_random_access() {
    BEGIN_TEST;
    EXPECT_TRUE(run_biotime("-random"));
    END_TEST;
}

BEGIN_TEST_CASE(biotime_tests)
RUN_TEST(test_biotime_linear_access)
RUN_TEST(test_biotime_random_access)
END_TEST_CASE(biotime_tests)

}

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
