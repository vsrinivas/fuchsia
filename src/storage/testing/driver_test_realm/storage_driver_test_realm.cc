// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/global.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

int main() {
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_LOGF(ERROR, "platform_driver_test_realm", "Failed to connect to Realm FIDL: %d",
            client_end.error_value());
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  fidl::Arena arena;
  fuchsia_driver_test::wire::RealmArgs args(arena);
  args.set_root_driver(arena, fidl::StringView("fuchsia-boot:///#driver/platform-bus.so"));
  auto wire_result = client->Start(std::move(args));
  if (wire_result.status() != ZX_OK) {
    FX_LOGF(ERROR, "platform_driver_test_realm", "Failed to call to Realm:Start: %d",
            wire_result.status());
    return 1;
  }
  if (wire_result->result.is_err()) {
    FX_LOGF(ERROR, "platform_driver_test_realm", "Realm:Start failed: %d",
            wire_result->result.err());
    return 1;
  }

  fbl::unique_fd out;
  zx_status_t status =
      device_watcher::RecursiveWaitForFile("/dev/sys/platform/00:00:2d/ramctl", &out);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "platform_driver_test_realm", "Failed to wait for ramctl: %d", status);
  }
  return 0;
}
