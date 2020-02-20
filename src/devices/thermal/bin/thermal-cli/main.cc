// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "thermal-cli.h"

constexpr char kUsageMessage[] = R"""(Usage: thermal-cli <device> <command>

    temp - Read the device's thermal sensor in degrees C
    fan [value] - Get or set the fan speed
    freq <big/little> [value] - Get or set the cluster frequency in Hz

    Example:
    thermal-cli /dev/class/thermal/000 freq big 1000000000
)""";

zx_status_t GetDeviceHandle(const char* path, zx::channel* handle) {
  fbl::unique_fd fd(open(path, O_RDWR));
  if (fd.get() < -1) {
    fprintf(stderr, "Failed to open thermal device: %d\n", fd.get());
    return ZX_ERR_IO;
  }

  zx_status_t status = fdio_get_service_handle(fd.release(), handle->reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to get FDIO handle for thermal device: %d\n", status);
  }
  return status;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    printf("%s", kUsageMessage);
    return 0;
  }

  zx::channel channel;
  zx_status_t status = GetDeviceHandle(argv[1], &channel);
  if (status != ZX_OK) {
    return 1;
  }

  ThermalCli thermal_cli(std::move(channel));

  if (strcmp(argv[2], "temp") == 0) {
    status = thermal_cli.PrintTemperature();
  } else if (strcmp(argv[2], "fan") == 0) {
    const char* value = argc >= 4 ? argv[3] : nullptr;
    status = thermal_cli.FanLevelCommand(value);
  } else if (strcmp(argv[2], "freq") == 0 && argc >= 4) {
    uint32_t cluster = fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN;
    if (strcmp(argv[3], "little") == 0) {
      cluster = fuchsia_hardware_thermal_PowerDomain_LITTLE_CLUSTER_POWER_DOMAIN;
    }

    const char* value = argc >= 5 ? argv[4] : nullptr;
    status = thermal_cli.FrequencyCommand(cluster, value);
  } else {
    printf("%s", kUsageMessage);
    return 1;
  }

  if (status != ZX_OK) {
    return 1;
  }
  return 0;
}
