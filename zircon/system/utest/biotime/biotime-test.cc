// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>

#include <utility>

#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

// This is a simple test of biotime (a block device IO performance
// measurement tool).  It runs biotime on a ramdisk and just checks that it
// returns a success status.
void run_biotime(fbl::Vector<const char*>&& args) {
  ramdisk_client_t* ramdisk;
  ASSERT_EQ(ramdisk_create(1024, 100, &ramdisk), ZX_OK);
  auto ac = fbl::MakeAutoCall([&] { EXPECT_EQ(ramdisk_destroy(ramdisk), 0); });

  args.insert(0, "/boot/bin/biotime");
  args.push_back(ramdisk_get_path(ramdisk));
  args.push_back(nullptr);  // fdio_spawn() wants a null-terminated array.

  zx::process process;
  ASSERT_EQ(fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, args[0], args.data(),
                       process.reset_and_get_address()),
            ZX_OK);

  // Wait for the process to exit.
  ASSERT_EQ(process.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr), ZX_OK);
  zx_info_process_t proc_info;
  ASSERT_EQ(process.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info), nullptr, nullptr),
            ZX_OK);
  ASSERT_EQ(proc_info.return_code, 0);
}

TEST(Biotime, LinearAccess) {
  fbl::Vector<const char*> args = {"-linear"};
  run_biotime(std::move(args));
}

TEST(Biotime, RandomAccess) {
  fbl::Vector<const char*> args = {"-random"};
  run_biotime(std::move(args));
}

TEST(Biotime, Write) {
  fbl::Vector<const char*> args = {"-write", "-live-dangerously"};
  run_biotime(std::move(args));
}

}  // namespace
