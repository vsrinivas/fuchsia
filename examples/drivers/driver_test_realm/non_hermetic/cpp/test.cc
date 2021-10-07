// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/global.h>

#include <gtest/gtest.h>

#include "src/devices/lib/device-watcher/cpp/device-watcher.h"

TEST(DdkFirmwaretest, DriverWasLoaded) {
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  ASSERT_TRUE(dev);

  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile(dev, "sys/test", &out));
}

int main(int argc, char **argv) {
  // Connect to DriverTestRealm.
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_LOGF(ERROR, "driver_test_realm_test", "Failed to connect to Realm FIDL: %d",
            client_end.error_value());
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  // Start the DriverTestRealm with correct arguments.
  fidl::Arena arena;
  auto wire_result = client.Start(fuchsia_driver_test::wire::RealmArgs(arena));
  if (wire_result.status() != ZX_OK) {
    FX_LOGF(ERROR, "driver_test_realm_test", "Failed to call to Realm:Start: %d",
            wire_result.status());
    return 1;
  }
  if (wire_result->result.is_err()) {
    FX_LOGF(ERROR, "driver_test_realm_test", "Realm:Start failed: %d", wire_result->result.err());
    return 1;
  }

  // Run the tests.
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
