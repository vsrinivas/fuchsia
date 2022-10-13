// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/sys/component/cpp/testing/realm_builder_types.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

class CompositeTest : public gtest::TestLoopFixture, public testing::WithParamInterface<bool> {};

TEST_P(CompositeTest, DriversExist) {
  // Create and build the realm.
  auto realm_builder = component_testing::RealmBuilder::Create();
  driver_test_realm::Setup(realm_builder);
  auto realm = realm_builder.Build(dispatcher());

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  ASSERT_EQ(ZX_OK, realm.Connect(driver_test_realm.NewRequest()));
  fuchsia::driver::test::Realm_Start_Result realm_result;

  fuchsia::driver::test::RealmArgs args;
  if (GetParam()) {
    args.set_use_driver_framework_v2(true);
    args.set_root_driver("fuchsia-boot:///#meta/test-parent-sys.cm");
  }

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
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_a", &out));
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_b", &out));
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/child_c", &out));
  if (GetParam()) {
    EXPECT_EQ(ZX_ERR_IO,
              device_watcher::RecursiveWaitForFile(root_fd, "composite_driver_v1", &out));
  } else {
    EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "composite_driver_v1", &out));
  }
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(
                       root_fd, "composite_driver_v1/composite_child", &out));

  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/fragment_a", &out));
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/fragment_b", &out));
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "sys/test/fragment_c", &out));
  if (GetParam()) {
    EXPECT_EQ(ZX_ERR_IO, device_watcher::RecursiveWaitForFile(root_fd, "composite-device", &out));
  } else {
    EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "composite-device", &out));
  }
  EXPECT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(root_fd, "composite-device/composite_child",
                                                        &out));
}

INSTANTIATE_TEST_SUITE_P(CompositeTest, CompositeTest, testing::Bool(),
                         [](const testing::TestParamInfo<bool>& info) {
                           if (info.param) {
                             return "DFv2";
                           }
                           return "DFv1";
                         });
