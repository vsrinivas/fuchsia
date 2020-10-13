// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/namespace.h>
#include <lib/memfs/memfs.h>

#include <fs-test-utils/fixture.h>
#include <unittest/unittest.h>

int main(int argc, char** argv) {
  devmgr_launcher::Args args = devmgr_integration_test::IsolatedDevmgr::DefaultArgs();
  args.disable_block_watcher = false;
  args.sys_device_driver = devmgr_integration_test::IsolatedDevmgr::kSysdevDriver;
  args.load_drivers.push_back(devmgr_integration_test::IsolatedDevmgr::kSysdevDriver);
  args.driver_search_paths.push_back("/boot/driver");

  devmgr_integration_test::IsolatedDevmgr devmgr;
  auto status = devmgr_integration_test::IsolatedDevmgr::Create(std::move(args), &devmgr);

  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }
  fbl::unique_fd fd;
  status = devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "misc/ramctl", &fd);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }

  fdio_ns_t* ns;
  status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }

  status = fdio_ns_bind_fd(ns, "/dev", devmgr.devfs_root().get());
  if (status != ZX_OK) {
    return EXIT_FAILURE;
  }

  return fs_test_utils::RunWithMemFs(
      [argc, argv]() { return unittest_run_all_tests(argc, argv) ? 0 : -1; });
}
