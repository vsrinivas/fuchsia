// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.driver.registrar/cpp/wire.h>
#include <fuchsia/pkg/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <stdio.h>

#include <fbl/string_printf.h>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <driver package url>\n", argv[0]);
    return -1;
  }

  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create channel, status %d\n", status);
    return status;
  }

  const auto svc_path = fbl::StringPrintf(
      "/svc/%s", fidl::DiscoverableProtocolName<fuchsia_driver_registrar::DriverRegistrar>);
  status = fdio_service_connect(svc_path.c_str(), remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "fdio_service_connect failed, pathc %s, status %d\n", svc_path.c_str(), status);
    return status;
  }
  fidl::WireSyncClient<fuchsia_driver_registrar::DriverRegistrar> client(std::move(local));

  auto resp =
      client->Register(fuchsia_pkg::wire::PackageUrl{fidl::StringView::FromExternal(argv[1])});
  if (!resp.ok()) {
    fprintf(stderr, "Failed to call DriverRegistrar::Register for driver package %s\n", argv[1]);
    return -1;
  }
  if (resp.value_NEW().is_error()) {
    fprintf(stderr, "DriverRegistrar::Register returned err %d for driver package %s\n",
            resp.value_NEW().error_value(), argv[1]);
    return -1;
  }
  printf("DriverRegistrar::Register successfully registered driver package %s\n", argv[1]);
  return 0;
}
