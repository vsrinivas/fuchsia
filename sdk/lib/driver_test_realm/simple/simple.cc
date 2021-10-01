// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <lib/syslog/global.h>

int main() {
  auto client_end = service::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_LOGF(ERROR, "simple_driver_test_realm", "Failed to connect to Realm FIDL: %d",
            client_end.error_value());
    return 1;
  }
  auto client = fidl::BindSyncClient(std::move(*client_end));

  fidl::Arena arena;
  auto wire_result = client.Start(fuchsia_driver_test::wire::RealmArgs(arena));
  if (wire_result.status() != ZX_OK) {
    FX_LOGF(ERROR, "simple_driver_test_realm", "Failed to call to Realm:Start: %d",
            wire_result.status());
    return 1;
  }
  if (wire_result->result.is_err()) {
    FX_LOGF(ERROR, "simple_driver_test_realm", "Realm:Start failed: %d", wire_result->result.err());
    return 1;
  }

  return 0;
}
