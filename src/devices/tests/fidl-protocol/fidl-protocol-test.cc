// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <fbl/unique_fd.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "lib/fdio/fd.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {
class FidlProtocolTest : public gtest::TestLoopFixture {};

TEST_F(FidlProtocolTest, ChildBinds) {
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

  // Wait for the child device to bind and appear. The child driver should bind with its string
  // properties. It will then make a call via FIDL and wait for the response before adding the child
  // device.
  fbl::unique_fd fd;
  status = device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child", &fd);
  ASSERT_EQ(status, ZX_OK);

  // Wait for the other child device to bind to prevent a shutdown race condition bug.
  status =
      device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child/isolated-child", &fd);
  ASSERT_EQ(status, ZX_OK);
}

TEST_F(FidlProtocolTest, ColocateFlagIsRespected) {
  // Verify that the colocate flag set on isolated-child in BUILD.gn is respected by driver manager.
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

  // Wait for the device to bind and appear.
  std::string parent_device_path = "sys/test/parent/child";
  std::string device_path = parent_device_path + "/isolated-child";
  fbl::unique_fd fd;
  status = device_watcher::RecursiveWaitForFile(root_fd, device_path.c_str(), &fd);
  ASSERT_EQ(status, ZX_OK);

  // Connect to the driver development server.
  fuchsia::driver::development::DriverDevelopmentSyncPtr driver_dev;
  status = realm.Connect(driver_dev.NewRequest());
  ASSERT_EQ(status, ZX_OK);

  // Get the child device's driver host.
  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  status = driver_dev->GetDeviceInfo({device_path}, iterator.NewRequest());
  ASSERT_EQ(status, ZX_OK);

  std::vector<fuchsia::driver::development::DeviceInfo> child_device_result;
  status = iterator->GetNext(&child_device_result);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(child_device_result.size(), 1ull);

  fuchsia::driver::development::DeviceInfo& device_info = child_device_result[0];
  uint64_t child_driver_host_koid = device_info.driver_host_koid();

  // Get the parent device's driver host.
  status = driver_dev->GetDeviceInfo({parent_device_path}, iterator.NewRequest());
  ASSERT_EQ(status, ZX_OK);

  std::vector<fuchsia::driver::development::DeviceInfo> parent_device_result;
  status = iterator->GetNext(&parent_device_result);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(parent_device_result.size(), 1ull);

  fuchsia::driver::development::DeviceInfo& parent_device_info = parent_device_result[0];
  uint64_t parent_driver_host_koid = parent_device_info.driver_host_koid();

  // Make sure the parent and child are in different hosts.
  ASSERT_NE(child_driver_host_koid, parent_driver_host_koid);
}

TEST_F(FidlProtocolTest, MustIsolateFlagIsPassed) {
  // Verify that the MUST_ISOLATE flag in the driver host is passed on to the driver manager.
  //
  // This is a regression test for fxb/112652.
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

  // Wait for the device to bind and appear.
  std::string device_path = "sys/test/parent/child/isolated-child";
  fbl::unique_fd fd;
  status = device_watcher::RecursiveWaitForFile(root_fd, device_path.c_str(), &fd);
  ASSERT_EQ(status, ZX_OK);

  // Connect to the driver development server.
  fuchsia::driver::development::DriverDevelopmentSyncPtr driver_dev;
  status = realm.Connect(driver_dev.NewRequest());
  ASSERT_EQ(status, ZX_OK);

  fuchsia::driver::development::DeviceInfoIteratorSyncPtr iterator;
  status = driver_dev->GetDeviceInfo({device_path}, iterator.NewRequest());
  ASSERT_EQ(status, ZX_OK);

  std::vector<fuchsia::driver::development::DeviceInfo> devices;
  status = iterator->GetNext(&devices);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(devices.size(), 1ull);

  // Use the driver development FIDL to check that the created device has the MUST_ISOLATE flag set.
  fuchsia::driver::development::DeviceInfo& device_info = devices[0];
  ASSERT_TRUE(static_cast<bool>(device_info.flags() &
                                fuchsia::driver::development::DeviceFlags::MUST_ISOLATE));
}

TEST_F(FidlProtocolTest, ChildBindsV2) {
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  auto realm = realm_builder.Build(dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::Realm_Start_Result realm_result;

  auto args = fuchsia::driver::test::RealmArgs();
  args.set_use_driver_framework_v2(true);
  args.set_root_driver("fuchsia-boot:///#meta/test-parent-sys.cm");
  ASSERT_EQ(ZX_OK, driver_test_realm->Start(std::move(args), &realm_result));
  ASSERT_FALSE(realm_result.is_err());

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  zx_status_t status = realm.Connect("dev", dev.NewRequest().TakeChannel());
  ASSERT_EQ(status, ZX_OK);

  fbl::unique_fd root_fd;
  status = fdio_fd_create(dev.TakeChannel().release(), root_fd.reset_and_get_address());
  ASSERT_EQ(status, ZX_OK);

  // Wait for the child device to bind and appear. The child driver should bind with its string
  // properties. It will then make a call via FIDL and wait for the response before adding the child
  // device.
  fbl::unique_fd fd;
  status = device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child", &fd);
  ASSERT_EQ(status, ZX_OK);

  // Wait for the other child device to bind to prevent a shutdown race condition bug.
  status =
      device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/child/isolated-child", &fd);
  ASSERT_EQ(status, ZX_OK);
}

}  // namespace
