// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

int main() {
  syslog::SetTags({"simple_driver_test_realm"});

  auto client_end = component::Connect<fuchsia_driver_test::Realm>();
  if (!client_end.is_ok()) {
    FX_SLOG(ERROR, "Failed to connect to Realm FIDL", KV("error", client_end.error_value()));
    return 1;
  }
  fidl::WireSyncClient client{std::move(*client_end)};

  fidl::Arena arena;
  auto wire_result = client->Start(fuchsia_driver_test::wire::RealmArgs(arena));
  if (wire_result.status() != ZX_OK) {
    FX_SLOG(ERROR, "Failed to connect to Realm:Start", KV("error", wire_result.status()));
    return 1;
  }
  if (wire_result.value().is_error()) {
    FX_SLOG(ERROR, "Realm:Start failed", KV("error", wire_result.value().error_value()));
    return 1;
  }

  return 0;
}
