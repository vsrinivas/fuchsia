// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/sys/component/cpp/service_client.h>

#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

TEST(NonbindableTest, DriversExist) {
  {
    // Connect to DriverTestRealm.
    auto client_end = component::Connect<fuchsia_driver_test::Realm>();
    ASSERT_EQ(client_end.status_value(), ZX_OK);
    fidl::WireSyncClient client{std::move(*client_end)};

    // Start the DriverTestRealm with correct arguments.
    fidl::Arena arena;
    auto args = fuchsia_driver_test::wire::RealmArgs::Builder(arena);
    args.use_driver_framework_v2(true);
    args.root_driver("fuchsia-boot:///#meta/test-parent-sys.cm");
    auto wire_result = client->Start(args.Build());
    ASSERT_EQ(wire_result.status(), ZX_OK);
  }

  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test/nonbindable/child", &out));
}
