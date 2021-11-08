// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>

#include <gtest/gtest.h>

#include "src/devices/lib/device-watcher/cpp/device-watcher.h"

TEST(DdkFirmwaretest, DriverWasLoaded) {
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  ASSERT_TRUE(dev);

  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(dev, "sys/test/ddk-fallback-test", &out));
}

int main(int argc, char **argv) {
  // Setup DriverTestRealm.
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  fidl::Arena allocator;
  auto response = client->Start(fuchsia_driver_test::wire::RealmArgs(allocator));
  if (response.status() != ZX_OK) {
    return 1;
  }
  if (response->result.is_err()) {
    return 1;
  }

  // Run the tests.
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
