// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device.manager/cpp/wire.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/devmgr-integration-test/fixture.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/service/llcpp/service.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/vmo.h>
#include <zircon/device/vfs.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

#include "src/lib/files/glob.h"

namespace devmgr_integration_test {

TEST(LauncherTest, DriverSearchPath) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

TEST(LauncherTest, LoadDrivers) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

TEST(LauncherTest, OutgoingServices) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));
  ASSERT_NE(devmgr.svc_root_dir().channel(), ZX_HANDLE_INVALID);

  // Test we are able to connect to at least one of the default services.
  auto svc_dir = service::ConnectAt<fuchsia_io::Directory>(devmgr.svc_root_dir(), "svc");
  ASSERT_OK(svc_dir.status_value());
  auto local = service::ConnectAt<fuchsia_device_manager::DebugDumper>(*svc_dir);
  ASSERT_OK(local.status_value());

  zx::vmo debug_vmo;
  zx::vmo vmo_dup;
  size_t vmo_size = 512 * 512;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &debug_vmo));
  ASSERT_OK(debug_vmo.duplicate(ZX_RIGHTS_IO | ZX_RIGHT_TRANSFER, &vmo_dup));

  auto result = fidl::BindSyncClient(std::move(*local))->DumpTree(std::move(vmo_dup));
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);

  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

}  // namespace devmgr_integration_test
