// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.gizmo.protocol/cpp/fidl.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <thread>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>
#include <zxtest/zxtest.h>

TEST(DriverTransportTest, ParentChildExists) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);

  // Create and build the realm.
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  auto realm = realm_builder.Build(loop.dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));

  auto args = fuchsia::driver::test::RealmArgs();
  args.set_use_driver_framework_v2(true);
  args.set_root_driver("fuchsia-boot:///#meta/test-parent-sys.cm");

  fuchsia::driver::test::Realm_Start_Result realm_result;
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(args), &realm_result));
  ASSERT_FALSE(realm_result.is_err());

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = realm.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd root_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), root_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  // Wait for parent driver.
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK,
            device_watcher::RecursiveWaitForFile(root_fd, "sys/test/transport-parent", &out));
  {
    // Turn the connection into FIDL.
    zx::channel chan;
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(out.get(), chan.reset_and_get_address()));
    fidl::ClientEnd<fuchsia_gizmo_protocol::TestingProtocol> client_end(std::move(chan));
    fidl::SyncClient client{std::move(client_end)};

    fidl::Result result = client->GetValue();
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(0x1234, result->response());
  }

  // Wait for child driver.
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(
                       root_fd, "sys/test/transport-parent/transport-child", &out));
  {
    // Turn the connection into FIDL.
    zx::channel chan;
    ASSERT_EQ(ZX_OK, fdio_get_service_handle(out.get(), chan.reset_and_get_address()));
    fidl::ClientEnd<fuchsia_gizmo_protocol::TestingProtocol> client_end(std::move(chan));
    fidl::SyncClient client{std::move(client_end)};

    fidl::Result result = client->GetValue();
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(0x1234, result->response());
  }
}
