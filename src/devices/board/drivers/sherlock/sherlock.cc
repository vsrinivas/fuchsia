// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sherlock.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

namespace sherlock {

static pbus_dev_t rtc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "rtc";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_RTC_FALLBACK;
  return dev;
}();

zx_status_t Sherlock::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  iommu_protocol_t iommu;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  pdev_board_info_t info;
  status = pbus_get_board_info(&pbus, &info);
  if (status != ZX_OK) {
    return status;
  }

  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Sherlock>(&ac, parent, &pbus, &iommu, info.pid);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("sherlock", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    return status;
  }

  // Start up our protocol helpers and platform devices.
  status = board->Start();
  if (status == ZX_OK) {
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = board.release();
  }
  return status;
}

int Sherlock::Thread() {
  // Load protocol implementation drivers first.
  if (SysmemInit() != ZX_OK) {
    zxlogf(ERROR, "SysmemInit() failed");
    return -1;
  }

  if (GpioInit() != ZX_OK) {
    zxlogf(ERROR, "GpioInit() failed");
    return -1;
  }

  if (RegistersInit() != ZX_OK) {
    zxlogf(ERROR, "RegistersInit() failed");
    return -1;
  }

  if (BoardInit() != ZX_OK) {
    zxlogf(ERROR, "BoardInit() failed");
    return -1;
  }

  if (ClkInit() != ZX_OK) {
    zxlogf(ERROR, "ClkInit() failed");
    return -1;
  }

  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "PowerInit() failed");
    return -1;
  }

  if (I2cInit() != ZX_OK) {
    zxlogf(ERROR, "I2cInit() failed");
  }

  if (CpuInit() != ZX_OK) {
    zxlogf(ERROR, "CpuInit() failed\n");
  }

  if (SpiInit() != ZX_OK) {
    zxlogf(ERROR, "SpiInit() failed");
  }

  if (CanvasInit() != ZX_OK) {
    zxlogf(ERROR, "CanvasInit() failed");
  }

  if (PwmInit() != ZX_OK) {
    zxlogf(ERROR, "PwmInit() failed");
  }

  if (ThermalInit() != ZX_OK) {
    zxlogf(ERROR, "ThermalInit() failed");
  }

  if (DsiInit() != ZX_OK) {
    zxlogf(ERROR, "DsiInit() failed");
  }

  if (DisplayInit() != ZX_OK) {
    zxlogf(ERROR, "DisplayInit() failed");
  }

  // Then the platform device drivers.
  if (UsbInit() != ZX_OK) {
    zxlogf(ERROR, "UsbInit() failed");
  }

  if (EmmcInit() != ZX_OK) {
    zxlogf(ERROR, "EmmcInit() failed");
  }

  if (SdioInit() != ZX_OK) {
    zxlogf(ERROR, "SdioInit() failed");
  }

  if (BluetoothInit() != ZX_OK) {
    zxlogf(ERROR, "BluetoothInit() failed");
  }

  if (CameraInit() != ZX_OK) {
    zxlogf(ERROR, "CameraInit() failed");
  }

  if (TeeInit() != ZX_OK) {
    zxlogf(ERROR, "TeeInit() failed");
  }

  if (VideoInit() != ZX_OK) {
    zxlogf(ERROR, "VideoInit() failed");
  }

  if (VideoEncInit() != ZX_OK) {
    zxlogf(ERROR, "VideoEncInit() failed");
  }

  if (HevcEncInit() != ZX_OK) {
    zxlogf(ERROR, "HevcEncInit() failed");
  }

  if (MaliInit() != ZX_OK) {
    zxlogf(ERROR, "MaliInit() failed");
  }

  if (NnaInit() != ZX_OK) {
    zxlogf(ERROR, "NnaInit() failed");
  }

  if (ButtonsInit() != ZX_OK) {
    zxlogf(ERROR, "ButtonsInit() failed");
  }

  if (AudioInit() != ZX_OK) {
    zxlogf(ERROR, "AudioInit() failed");
  }

  if (TouchInit() != ZX_OK) {
    zxlogf(ERROR, "TouchInit() failed");
    return -1;
  }

  if (LightInit() != ZX_OK) {
    zxlogf(ERROR, "LightInit() failed");
    return -1;
  }

  if (OtRadioInit() != ZX_OK) {
    zxlogf(ERROR, "OtRadioInit() failed");
  }

  if (SecureMemInit() != ZX_OK) {
    zxlogf(ERROR, "SecureMbemInit failed");
  }

  if (BacklightInit() != ZX_OK) {
    zxlogf(ERROR, "BacklightInit() failed");
  }

  if (RamCtlInit() != ZX_OK) {
    zxlogf(ERROR, "RamCtlInit failed");
  }

  if (ThermistorInit() != ZX_OK) {
    zxlogf(ERROR, "ThermistorInit failed");
  }

  zx_status_t status = pbus_.DeviceAdd(&rtc_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed for RTC - error %d", __func__, status);
    return -1;
  }

  return 0;
}

zx_status_t Sherlock::PowerInit() {
  if (pid_ == PDEV_PID_LUIS) {
    return LuisPowerInit();
  } else if (pid_ == PDEV_PID_SHERLOCK) {
    // Sherlock does not implement a power driver yet.
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sherlock::CpuInit() {
  if (pid_ == PDEV_PID_LUIS) {
    return LuisCpuInit();
  } else if (pid_ == PDEV_PID_SHERLOCK) {
    return SherlockCpuInit();
  }
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Sherlock::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Sherlock*>(arg)->Thread(); }, this,
      "sherlock-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Sherlock::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Sherlock::Create;
  return ops;
}();

}  // namespace sherlock

// clang-format off
ZIRCON_DRIVER_BEGIN(sherlock, sherlock::driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_SHERLOCK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_LUIS),
ZIRCON_DRIVER_END(sherlock)
