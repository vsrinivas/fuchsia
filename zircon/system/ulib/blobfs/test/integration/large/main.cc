// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/sysinfo/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/memfs/memfs.h>

#include <memory>

#include <fs/test_support/environment.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"

namespace {

// Filters out platforms where there's very little upside in running this test.
// TODO(fxbug.dev/39752): This should be controlled with build options.
bool RunInThisPlatform() {
  fbl::unique_fd sysinfo(open("/svc/fuchsia.sysinfo.SysInfo", O_RDONLY));
  fdio_cpp::FdioCaller caller(std::move(sysinfo));
  auto result = ::llcpp::fuchsia::sysinfo::SysInfo::Call::GetBoardName(caller.channel());
  if (result.status() != ZX_OK || result->status != ZX_OK) {
    return false;
  }

  if (strncmp(result->name.data(), "astro", result->name.size()) == 0) {
    return false;
  }

  if (strncmp(result->name.data(), "sherlock", result->name.size()) == 0) {
    return false;
  }
  return true;
}

}  // namespace

// The test can operate over either a ramdisk, or a real device. Initialization
// of that device happens at the test environment level, but the test fixtures
// must be able to see it.
fs::Environment* fs::g_environment;

int main(int argc, char** argv) {
  const char kHelp[] = "Blobfs large integration tests";
  fs::Environment::TestConfig config = {};
  if (!config.GetOptions(argc, argv)) {
    printf("%s\n%s\n", kHelp, config.HelpMessage());
    return -1;
  }
  if (config.show_help) {
    printf("%s\n%s\n", kHelp, config.HelpMessage());
  }

  if (!RunInThisPlatform()) {
    printf("Not running on this platform\n");
    return 0;
  }

  config.mount_path = kMountPath;
  config.ramdisk_block_count = 1 << 18;  // 128 MB.
  config.format_type = DISK_FORMAT_BLOBFS;
  config.write_compression_level = 3;

  auto parent = std::make_unique<fs::Environment>(config);
  fs::g_environment = parent.get();

  // Initialize a tmpfs instance to "hold" the mounted blobfs.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (loop.StartThread() != ZX_OK) {
    printf("Unable to initialize local tmpfs loop\n");
    return -1;
  }
  memfs_filesystem_t* fs;
  if (memfs_install_at(loop.dispatcher(), "/blobfs-tmp", &fs) != ZX_OK) {
    printf("Unable to install local tmpfs\n");
    return -1;
  }

  zxtest::Runner::GetInstance()->AddGlobalTestEnvironment(std::move(parent));

  int result = RUN_ALL_TESTS(argc, argv);
  loop.Shutdown();
  memfs_uninstall_unsafe(fs, "/blobfs-tmp");
  return result;
}
