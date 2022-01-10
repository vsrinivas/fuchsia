// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.hardware.radar/cpp/wire.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>

#include "radarutil.h"

namespace {
constexpr char kRadarDevicePath[] = "/dev/class/radar/000";
}  // namespace

int main(int argc, char** argv) {
  fbl::unique_fd device(open(kRadarDevicePath, O_RDWR));
  if (!device.is_valid()) {
    fprintf(stderr, "Failed to open %s: %s\n", kRadarDevicePath, strerror(errno));
    return 1;
  }

  fidl::ClientEnd<fuchsia_hardware_radar::RadarBurstReaderProvider> provider_client;
  zx_status_t status =
      fdio_get_service_handle(device.release(), provider_client.channel().reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get service handle: %s\n", zx_status_get_string(status));
    return 1;
  }

  status = radarutil::RadarUtil::Run(argc, argv, std::move(provider_client));
  return status == ZX_OK ? 0 : 1;
}
