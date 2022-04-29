// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

class CompositeTest : public gtest::TestLoopFixture {};

TEST_F(CompositeTest, DriversExist) {
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
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_a", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_b", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_c", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "composite_driver_v1", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(
                       root_fd, "composite_driver_v1/composite_child", &out));
}

TEST_F(CompositeTest, DriversExist_DFv2) {
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
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_a", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_b", &out));
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_c", &out));
  // In DFv2, the composite device is added to the topo path of the primary node.
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_a/composite_child",
                                                        &out));
}
