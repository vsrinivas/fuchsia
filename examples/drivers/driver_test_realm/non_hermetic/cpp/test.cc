// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>
#include <sdk/lib/device-watcher/cpp/device-watcher.h>

// [START example]
TEST(DdkFirmwaretest, DriverWasLoaded) {
  fbl::unique_fd out;
  ASSERT_EQ(ZX_OK, device_watcher::RecursiveWaitForFile("/dev/sys/test", &out));
}

int main(int argc, char **argv) {
  syslog::SetTags({"driver_test_realm_test"});

  // Connect to DriverTestRealm.
  auto client_end = component::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_SLOG(ERROR, "Failed to connect to Realm FIDL", KV("error", client_end.error_value()));
    return 1;
  }
  fidl::WireSyncClient client{std::move(*client_end)};

  // Start the DriverTestRealm with correct arguments.
  auto wire_result = client->Start(fuchsia_driver_test::wire::RealmArgs());
  if (wire_result.status() != ZX_OK) {
    FX_SLOG(ERROR, "Failed to call to Realm:Start", KV("status", wire_result.status()));
    return 1;
  }
  if (wire_result.value().is_error()) {
    FX_SLOG(ERROR, "Realm:Start failed", KV("status", wire_result.value().error_value()));
    return 1;
  }

  // Run the tests.
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
// [END example]
