// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>
#include <fuchsia/hardware/thermal/c/fidl.h>

#include "thermal-cli.h"

static zx_status_t read_argument_checked(const char* arg, uint32_t* out) {
  char* end;
  long value = strtol(arg, &end, 10);
  if (*end != '\0') {
    return ZX_ERR_INVALID_ARGS;
  }
  if (value < 0 || value >= UINT32_MAX) {
    return ZX_ERR_INVALID_ARGS;
  }
  *out = static_cast<uint32_t>(value);
  return ZX_OK;
}

zx_status_t ThermalCli::PrintTemperature() {
  zx_status_t status, status2;
  float temp = 0.0f;
  status = fuchsia_hardware_thermal_DeviceGetTemperatureCelsius(channel_.get(), &status2, &temp);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "DeviceGetTemperatureCelsius failed: %d %d\n", status, status2);
    return status == ZX_OK ? status2 : status;
  }

  printf("Temperature: %0.03f\n", temp);
  return ZX_OK;
}

int ThermalCli::FanLevelCommand(const char* value) {
  zx_status_t status, status2;

  if (value == nullptr) {
    uint32_t fan_level;
    status = fuchsia_hardware_thermal_DeviceGetFanLevel(channel_.get(), &status2, &fan_level);
    if (status != ZX_OK || status2 != ZX_OK) {
      fprintf(stderr, "DeviceSetFanLevel failed: %d %d\n", status, status2);
      return status == ZX_OK ? status2 : status;
    }

    printf("Fan level: %u\n", fan_level);
  } else {
    uint32_t fan_level;
    status = read_argument_checked(value, &fan_level);
    if (status != ZX_OK) {
      fprintf(stderr, "Invalid fan level argument: %s\n", value);
      return status;
    }

    status = fuchsia_hardware_thermal_DeviceSetFanLevel(channel_.get(),
                                                        static_cast<uint32_t>(fan_level), &status2);
    if (status != ZX_OK || status2 != ZX_OK) {
      fprintf(stderr, "DeviceSetFanLevel failed: %d %d\n", status, status2);
      return status == ZX_OK ? status2 : status;
    }
  }

  return ZX_OK;
}

zx_status_t ThermalCli::FrequencyCommand(uint32_t cluster, const char* value) {
  zx_status_t status, status2;
  fuchsia_hardware_thermal_OperatingPoint op_info;
  status = fuchsia_hardware_thermal_DeviceGetDvfsInfo(channel_.get(), cluster, &status2, &op_info);
  if (status != ZX_OK || status2 != ZX_OK) {
    fprintf(stderr, "DeviceGetDvfsInfo failed: %d %d\n", status, status2);
    return status == ZX_OK ? status2 : status;
  } else if (op_info.count > fuchsia_hardware_thermal_MAX_DVFS_OPPS) {
    fprintf(stderr, "DeviceGetDvfsInfo reported too many operating points\n");
    return ZX_ERR_BAD_STATE;
  }

  if (value == nullptr) {
    uint16_t op_idx;
    status = fuchsia_hardware_thermal_DeviceGetDvfsOperatingPoint(channel_.get(), cluster, &status2,
                                                                  &op_idx);
    if (status != ZX_OK || status2 != ZX_OK) {
      fprintf(stderr, "DeviceGetDvfsOperatingPoint failed: %d %d\n", status, status2);
      return status == ZX_OK ? status2 : status;
    } else if (op_idx > op_info.count) {
      fprintf(stderr, "DeviceGetDvfsOperatingPoint reported an invalid operating point\n");
    }

    printf("Current frequency: %u Hz\n", op_info.opp[op_idx].freq_hz);

    printf("Operating points:\n");
    for (uint32_t i = 0; i < op_info.count; i++) {
      printf("%u Hz\n", op_info.opp[i].freq_hz);
    }
  } else {
    uint32_t freq;
    status = read_argument_checked(value, &freq);
    if (status != ZX_OK) {
      fprintf(stderr, "Invalid frequency argument: %s\n", value);
      return status;
    }

    uint16_t op_idx;
    for (op_idx = 0; op_idx < op_info.count; op_idx++) {
      if (op_info.opp[op_idx].freq_hz == freq) {
        break;
      }
    }

    if (op_idx >= op_info.count) {
      fprintf(stderr, "No operating point found for %u Hz\n", freq);

      fprintf(stderr, "Operating points:\n");
      for (uint32_t i = 0; i < op_info.count; i++) {
        fprintf(stderr, "%u Hz\n", op_info.opp[i].freq_hz);
      }
      return ZX_ERR_NOT_FOUND;
    }

    status = fuchsia_hardware_thermal_DeviceSetDvfsOperatingPoint(channel_.get(), op_idx, cluster,
                                                                  &status2);
    if (status != ZX_OK || status2 != ZX_OK) {
      fprintf(stderr, "DeviceSetDvfsOperatingPoint failed: %d %d\n", status, status2);
      return status == ZX_OK ? status2 : status;
    }
  }

  return ZX_OK;
}
