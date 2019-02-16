// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <ramdevice-client/ramdisk.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <unittest/unittest.h>

#include <utility>

namespace {

// This is a simple test of biotime (a block device IO performance
// measurement tool).  It runs biotime on a ramdisk and just checks that it
// returns a success status.
bool run_biotime(fbl::Vector<const char*>&& args) {
    ramdisk_client_t* ramdisk;
    ASSERT_EQ(ramdisk_create(1024, 100, &ramdisk), ZX_OK);
    auto ac = fbl::MakeAutoCall([&] {
        EXPECT_EQ(ramdisk_destroy(ramdisk), 0);
    });

    args.insert(0, "/boot/bin/biotime");
    args.push_back(ramdisk_get_path(ramdisk));
    args.push_back(nullptr); // fdio_spawn() wants a null-terminated array.

    zx::process process;
    ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, args[0],
                         args.get(), process.reset_and_get_address()), ZX_OK);

    // Wait for the process to exit.
    ASSERT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(),
                               nullptr), ZX_OK);
    zx_info_process_t proc_info;
    ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                               nullptr, nullptr), ZX_OK);
    ASSERT_EQ(proc_info.return_code, 0);
    return true;
}

bool TestBiotimeLinearAccess() {
    BEGIN_TEST;

    fbl::Vector<const char*> args = {"-linear"};
    EXPECT_TRUE(run_biotime(std::move(args)));

    END_TEST;
}

bool TestBiotimeRandomAccess() {
    BEGIN_TEST;

    fbl::Vector<const char*> args = {"-random"};
    EXPECT_TRUE(run_biotime(std::move(args)));

    END_TEST;
}

bool TestBiotimeWrite() {
    BEGIN_TEST;

    fbl::Vector<const char*> args = {"-write", "-live-dangerously"};
    EXPECT_TRUE(run_biotime(std::move(args)));

    END_TEST;
}

BEGIN_TEST_CASE(biotime_tests)
RUN_TEST(TestBiotimeLinearAccess)
RUN_TEST(TestBiotimeRandomAccess)
RUN_TEST(TestBiotimeWrite)
END_TEST_CASE(biotime_tests)

}
