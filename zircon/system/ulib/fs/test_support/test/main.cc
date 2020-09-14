// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fs-management/mount.h>
#include <fs/test_support/environment.h>
#include <zxtest/zxtest.h>

fs::Environment* fs::g_environment;

int main(int argc, char** argv) {
  fs::Environment::TestConfig config = {};
  config.is_packaged = false;
  config.mount_path = "/tmp/foo";
  config.format_type = DISK_FORMAT_BLOBFS;
  config.ramdisk_block_count = 1 << 16;  // 32 MB.

  auto parent = std::make_unique<fs::Environment>(config);
  fs::g_environment = parent.get();

  zxtest::Runner::GetInstance()->AddGlobalTestEnvironment(std::move(parent));

  return RUN_ALL_TESTS(argc, argv);
}
