// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/sherlock/sherlock.h"

#include <assert.h>
#include <fuchsia/hardware/gpio/c/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/devices/board/drivers/sherlock/sherlock-gpios.h"

#if IS_LUIS
#include "src/devices/board/drivers/sherlock/luis-bind.h"
#else
#include "src/devices/board/drivers/sherlock/sherlock-bind.h"
#endif

namespace sherlock {

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

  status = board->DdkAdd(ddk::DeviceAddArgs("sherlock")
                             .set_flags(DEVICE_ADD_NON_BINDABLE)
                             .set_inspect_vmo(board->inspector_.DuplicateVmo()));
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

uint8_t Sherlock::GetBoardRev() {
  if (!board_rev_) {
    uint8_t id0, id1, id2;

    gpio_impl_.ConfigIn(GPIO_HW_ID0, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_HW_ID1, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_HW_ID2, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_HW_ID0, &id0);
    gpio_impl_.Read(GPIO_HW_ID1, &id1);
    gpio_impl_.Read(GPIO_HW_ID2, &id2);

    board_rev_.emplace(id0 | (id1 << 1) | (id2 << 2));
  }

  return *board_rev_;
}

uint8_t Sherlock::GetBoardOption() {
  if (!board_option_) {
    uint8_t id3, id4;

    gpio_impl_.ConfigIn(GPIO_HW_ID3, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_HW_ID4, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_HW_ID3, &id3);
    gpio_impl_.Read(GPIO_HW_ID4, &id4);

    board_option_.emplace(id3 | (id4 << 1));
  }

  return *board_option_;
}

uint8_t Sherlock::GetDisplayVendor() {
  if (!display_vendor_) {
    uint8_t value;
    gpio_impl_.ConfigIn(GPIO_PANEL_DETECT, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_PANEL_DETECT, &value);
    display_vendor_.emplace(value);
  }

  return *display_vendor_;
}

uint8_t Sherlock::GetDdicVersion() {
  if (!ddic_version_) {
    uint8_t value;
    gpio_impl_.ConfigIn(GPIO_DDIC_DETECT, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_DDIC_DETECT, &value);
    ddic_version_.emplace(value);
  }

  return *ddic_version_;
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

  root_ = inspector_.GetRoot().CreateChild("sherlock_board_driver");
  board_rev_property_ = root_.CreateUint("board_build", GetBoardRev());
  board_option_property_ = root_.CreateUint("board_option", GetBoardOption());
  // PANEL_DETECT -> DISP_SOC_ID1
  // DDIC_DETECT -> DISP_SOC_ID2
  display_id_property_ =
      root_.CreateUint("display_id", GetDisplayVendor() | (GetDdicVersion() << 1));

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

ZIRCON_DRIVER(sherlock, sherlock::driver_ops, "zircon", "0.1");
