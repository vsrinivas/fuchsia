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

#include <zxtest/zxtest.h>

#include "src/lib/files/glob.h"

namespace devmgr_integration_test {

TEST(LauncherTest, DriverSearchPath) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

TEST(LauncherTest, LoadDrivers) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.load_drivers.push_back("/boot/driver/test.so");
  args.load_drivers.push_back("/boot/driver/test-parent-sys.so");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

TEST(LauncherTest, Namespace) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/test_drivers/test-parent-sys.so";
  args.driver_search_paths.push_back("/test_drivers");
  args.driver_search_paths.push_back("/test_drivers/test");

  zx::channel bootfs_client, bootfs_server;
  ASSERT_OK(zx::channel::create(0, &bootfs_client, &bootfs_server));
  ASSERT_OK(fdio_open("/pkg/driver", ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                      bootfs_server.release()));

  args.flat_namespace.push_back(std::make_pair("/test_drivers", std::move(bootfs_client)));

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  fbl::unique_fd fd;
  ASSERT_OK(
      devmgr_integration_test::RecursiveWaitForFile(devmgr.devfs_root(), "sys/test/test", &fd));
}

TEST(LauncherTest, OutgoingServices) {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.driver_search_paths.push_back("/boot/driver");

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

  auto result = fidl::BindSyncClient(std::move(*local)).DumpTree(std::move(vmo_dup));
  ASSERT_OK(result.status());
  ASSERT_OK(result->status);
}

TEST(LauncherTest, ExposeDevfsToHub) {
  // Setup outgoing directory. This should be done only once in the test component.
  // Ideally done during test setup, but since this is the only test case using outgoing directory
  // it is done here.
  async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
  auto context = sys::ComponentContext::Create();
  context->outgoing()->ServeFromStartupInfo(loop.dispatcher());
  loop.StartThread();

  // Create devmgr instance
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/test-parent-sys.so";
  args.driver_search_paths.push_back("/boot/driver");

  IsolatedDevmgr devmgr;
  ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr));

  // Add devfs to out directory
  devmgr.AddDevfsToOutgoingDir(context->outgoing()->root_dir());

  // Verify that devfs is accessible in the outgoing directory
  constexpr char kGlob[] = "/hub/c/devmgr-integration-test.cmx/*/out/dev";
  files::Glob glob(kGlob);
  EXPECT_EQ(glob.size(), 1u);
  loop.Shutdown();
}

}  // namespace devmgr_integration_test
