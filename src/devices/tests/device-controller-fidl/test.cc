// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.sample/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class DeviceControllerFidl : public gtest::TestLoopFixture {};

TEST_F(DeviceControllerFidl, ControllerTest) {
  // Create and build the realm.
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  auto realm = realm_builder.Build(dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::Realm_Start_Result realm_result;
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(fuchsia::driver::test::RealmArgs(), &realm_result));
  ASSERT_FALSE(realm_result.is_err());

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = realm.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd root_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), root_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  // Wait for driver.
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/sample_driver", &out));

  // Connect to the controller.
  auto endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);

  zx::channel dev_channel;
  fdio_fd_clone(root_fd.get(), dev_channel.reset_and_get_address());
  fdio_service_connect_at(dev_channel.get(), "sys/test/sample_driver",
                          endpoints->server.TakeChannel().release());

  auto client = fidl::WireSyncClient(
      fidl::ClientEnd<fuchsia_device::Controller>(std::move(endpoints->client)));

  auto result = client->GetTopologicalPath();
  ASSERT_EQ(result->value()->path.get(), "/dev/sys/test/sample_driver");

  // Get the underlying device connection.
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_sample::Echo>();
    ASSERT_EQ(client->ConnectToDeviceFidl(endpoints->server.TakeChannel()).status(), ZX_OK);

    auto echo = fidl::WireSyncClient(std::move(endpoints->client));

    std::string_view sent_string = "hello";
    auto result = echo->EchoString(fidl::StringView::FromExternal(sent_string));
    ASSERT_EQ(ZX_OK, result.status());
    ASSERT_EQ(sent_string, result.value().response.get());
  }
}

TEST_F(DeviceControllerFidl, ControllerTestDfv2) {
  // Create and build the realm.
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  auto realm = realm_builder.Build(dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::Realm_Start_Result realm_result;
  auto args = fuchsia::driver::test::RealmArgs();
  args.set_use_driver_framework_v2(true);
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(args), &realm_result));
  ASSERT_FALSE(realm_result.is_err());

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = realm.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd root_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), root_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  // Wait for driver.
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/sample_driver", &out));

  auto endpoints = fidl::CreateEndpoints<fuchsia_device::Controller>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);

  zx::channel dev_channel;
  fdio_fd_clone(root_fd.get(), dev_channel.reset_and_get_address());
  fdio_service_connect_at(dev_channel.get(), "sys/test/sample_driver",
                          endpoints->server.TakeChannel().release());

  auto client = fidl::WireSyncClient(
      fidl::ClientEnd<fuchsia_device::Controller>(std::move(endpoints->client)));

  auto result = client->GetTopologicalPath();
  ASSERT_EQ(result->value()->path.get(), "/dev/sys/test/sample_driver");

  // Get the underlying device connection.
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_sample::Echo>();
    ASSERT_EQ(client->ConnectToDeviceFidl(endpoints->server.TakeChannel()).status(), ZX_OK);

    auto echo = fidl::WireSyncClient(std::move(endpoints->client));

    std::string_view sent_string = "hello";
    auto result = echo->EchoString(fidl::StringView::FromExternal(sent_string));
    ASSERT_EQ(ZX_OK, result.status());
    ASSERT_EQ(sent_string, result.value().response.get());
  }
}
