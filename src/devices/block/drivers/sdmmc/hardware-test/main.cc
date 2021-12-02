// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.gpio/cpp/wire.h>
#include <fidl/fuchsia.hardware.i2c/cpp/wire.h>
#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <stdio.h>

#include <zxtest/zxtest.h>

#include "sdmmc-test-device-controller.h"

static zx_status_t CheckControllerIdAndVersion() {
  // TODO: Extract these into a device-specific config
  constexpr char kControllerI2cDevicePath[] = "/dev/sys/platform/05:00:2/aml-i2c/i2c/i2c-1-50";
  constexpr uint8_t kExpectedCoreVersion = 1;
  constexpr char kExpectedControllerId[] = "SDIO";

  fidl::WireSyncClient i2c =
      sdmmc::GetFidlClient<fuchsia_hardware_i2c::Device2>(kControllerI2cDevicePath);
  sdmmc::SdmmcTestDeviceController controller(std::move(i2c));
  if (!controller.is_valid()) {
    fprintf(stderr, "Failed to connect to %s\n", kControllerI2cDevicePath);
    return ZX_ERR_IO;
  }

  // TODO: Re-enable these checks
#if 1
  const auto id = controller.GetId();
  if (id.is_error()) {
    fprintf(stderr, "Failed to read controller ID: %s\n", id.status_string());
    return id.error_value();
  }

  if (id->size() != strlen(kExpectedControllerId) ||
      memcmp(id->data(), kExpectedControllerId, id->size()) != 0) {
    fprintf(stderr, "Invalid controller ID\n");
    return ZX_ERR_BAD_STATE;
  }

  const auto version = controller.GetCoreVersion();
  if (version.is_error()) {
    fprintf(stderr, "Failed to read controller version: %s\n", id.status_string());
    return version.error_value();
  }

  if (version.value() != kExpectedCoreVersion) {
    fprintf(stderr, "Unexpected core version %u\n", version.value());
    return ZX_ERR_BAD_STATE;
  }
#endif

  return ZX_OK;
}

int main(int argc, char** argv) {
  // TODO: Extract these into a device-specific config
  constexpr char kSysInfoPath[] = "/svc/fuchsia.sysinfo.SysInfo";
  constexpr char kExpectedBoardName[] = "vim3";

  constexpr char kPowerGpioDevicePath[] = "/dev/gpio-expander/ti-tca6408a/gpio-107";

  setlinebuf(stdout);

  fidl::WireSyncClient sysinfo = sdmmc::GetFidlClient<fuchsia_sysinfo::SysInfo>(kSysInfoPath);
  if (!sysinfo.is_valid()) {
    fprintf(stderr, "Failed to connect to %s\n", kSysInfoPath);
    return 1;
  }

  const auto board_name = sysinfo->GetBoardName();
  if (!board_name.ok() || board_name->status != ZX_OK) {
    fprintf(stderr, "Failed to get board name\n");
    return 1;
  }

  if (board_name->name.size() != strlen(kExpectedBoardName) ||
      memcmp(board_name->name.data(), kExpectedBoardName, board_name->name.size()) != 0) {
    printf("Detected unsupported board %s, skipping tests\n", kExpectedBoardName);
    return 0;
  }

  if (CheckControllerIdAndVersion() != ZX_OK) {
    return 1;
  }

  fidl::WireSyncClient voltage_gpio =
      sdmmc::GetFidlClient<fuchsia_hardware_gpio::Gpio>(kPowerGpioDevicePath);
  if (!voltage_gpio.is_valid()) {
    fprintf(stderr, "Failed to connect to %s\n", kPowerGpioDevicePath);
    return 1;
  }

  // Set the bus voltage to 1.8V.
  const auto response = voltage_gpio->ConfigOut(1);
  if (!response.ok() || response->result.is_err()) {
    fprintf(stderr, "Failed to set SDMMC bus voltage\n");
    return 1;
  }

  // Sleep for some time to allow the voltage to stabilize.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));

  const int ret = RUN_ALL_TESTS(argc, argv);

  // Set the bus voltage back to 3.3V.
  voltage_gpio->ConfigOut(0);
  return ret;
}
