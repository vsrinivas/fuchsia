// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <zircon/device/vfs.h>
#include <zxtest/zxtest.h>

namespace devmgr_integration_test {

TEST(LauncherTest, DriverSearchPath) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

TEST(LauncherTest, LoadDrivers) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.load_drivers.push_back("/boot/driver/test.so");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

TEST(LauncherTest, Namespace) {
  devmgr_launcher::Args args;
  args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
  args.driver_search_paths.push_back("/test_drivers");

  zx::channel bootfs_client, bootfs_server;
  ASSERT_OK(zx::channel::create(0, &bootfs_client, &bootfs_server));
  ASSERT_OK(fdio_open("/boot/driver", ZX_FS_RIGHT_READABLE, bootfs_server.release()));

  args.flat_namespace.push_back(std::make_pair("/test_drivers", std::move(bootfs_client)));

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "test/test", &fd));
}

}  // namespace devmgr_integration_test
