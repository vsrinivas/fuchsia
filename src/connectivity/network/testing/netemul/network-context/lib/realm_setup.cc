// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/testing/netemul/network-context/lib/realm_setup.h"

#include <fcntl.h>
#include <fidl/fuchsia.driver.test/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>

#include <sdk/lib/device-watcher/cpp/device-watcher.h>

namespace netemul {

constexpr char kTapctlRelativePath[] = "sys/test/tapctl";

zx::result<fbl::unique_fd> StartDriverTestRealm() {
  zx::result client_end = component::Connect<fuchsia_driver_test::Realm>();
  if (client_end.is_error()) {
    return zx::error(client_end.status_value());
  }
  fidl::WireSyncClient client{std::move(client_end.value())};
  fidl::WireResult fidl_result = client->Start(fuchsia_driver_test::wire::RealmArgs());
  if (!fidl_result.ok()) {
    return zx::error(fidl_result.status());
  }
  const fit::result result = fidl_result.value();
  if (result.is_error()) {
    return zx::error(result.error_value());
  }

  // Wait for the driver to be enumerated in '/dev'.
  fbl::unique_fd dev(open("/dev", O_RDONLY));
  if (!dev) {
    switch (errno) {
      case ENOENT:
        return zx::error(ZX_ERR_NOT_FOUND);
      default:
        ZX_PANIC("unexpected error opening '/dev': %s", strerror(errno));
    }
  }
  fbl::unique_fd out;
  zx_status_t status = device_watcher::RecursiveWaitForFile(dev, kTapctlRelativePath, &out);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(dev));
}

}  // namespace netemul
