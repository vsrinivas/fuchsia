// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/driver/development/cpp/fidl.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>

#include <fbl/unique_fd.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace {
class BanjoProxyTest : public gtest::TestLoopFixture {};

TEST_F(BanjoProxyTest, ChildBinds) {
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

  // Wait for the child device to bind and appear. The child driver will then make a call via
  // proxied Banjo and wait for the response before adding the child device.
  fbl::unique_fd fd;
  status = device_watcher::RecursiveWaitForFile(root_fd, "sys/test/parent/composite/child", &fd);
  ASSERT_EQ(status, ZX_OK);
}

}  // namespace
