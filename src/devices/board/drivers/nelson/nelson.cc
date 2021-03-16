// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/nelson/nelson.h"

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

#include "src/devices/board/drivers/nelson/nelson-bind.h"
#include "src/devices/board/drivers/nelson/nelson-gpios.h"

namespace nelson {

static const pbus_dev_t rtc_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "rtc";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_RTC_FALLBACK;
  return dev;
}();

uint32_t Nelson::GetBoardRev() {
  if (!board_rev_) {
    uint32_t board_rev;
    uint8_t id0, id1, id2;

    gpio_impl_.ConfigIn(GPIO_HW_ID0, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_HW_ID1, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_HW_ID2, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_HW_ID0, &id0);
    gpio_impl_.Read(GPIO_HW_ID1, &id1);
    gpio_impl_.Read(GPIO_HW_ID2, &id2);
    board_rev = id0 + (id1 << 1) + (id2 << 2);

    if (board_rev >= MAX_SUPPORTED_REV) {
      // We have detected a new board rev. Print this warning just in case the
      // new board rev requires additional support that we were not aware of
      zxlogf(INFO, "Unsupported board revision detected (%d)", board_rev);
    }

    board_rev_.emplace(board_rev);
  }

  return *board_rev_;
}

uint32_t Nelson::GetDisplayId() {
  if (!display_id_) {
    uint8_t id0, id1;
    gpio_impl_.ConfigIn(GPIO_DISPLAY_ID0, GPIO_NO_PULL);
    gpio_impl_.ConfigIn(GPIO_DISPLAY_ID1, GPIO_NO_PULL);
    gpio_impl_.Read(GPIO_DISPLAY_ID0, &id0);
    gpio_impl_.Read(GPIO_DISPLAY_ID1, &id1);
    display_id_.emplace(id0 | (id1 << 1));
  }

  return *display_id_;
}

bool Nelson::Is9365Ddic() {
  // On DVT or later, GPIOZ_11 indicates whether the 9364 (1) or 9365 (0) DDIC is present. Only the
  // 9364 is used before DVT.
  if (GetBoardRev() >= BOARD_REV_DVT) {
    return (GetDisplayId() & 1) == 0;
  }
  return false;
}

int Nelson::Thread() {
  zx_status_t status;

  // Sysmem is started early so zx_vmo_create_contiguous() works.
  if ((status = SysmemInit()) != ZX_OK) {
    zxlogf(ERROR, "%s: SysmemInit() failed: %d", __func__, status);
    return status;
  }

  if ((status = GpioInit()) != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit() failed: %d", __func__, status);
    return status;
  }

  // Once gpio is up and running, let's populate board revision
  pbus_board_info_t info = {};
  info.board_revision = GetBoardRev();
  status = pbus_.SetBoardInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PBusSetBoardInfo failed: %d", __func__, status);
  }
  zxlogf(INFO, "Detected board rev 0x%x", info.board_revision);

  if ((info.board_revision != BOARD_REV_P1) && (info.board_revision != BOARD_REV_P2) &&
      (info.board_revision != BOARD_REV_EVT) && (info.board_revision != BOARD_REV_DVT) &&
      (info.board_revision != BOARD_REV_DVT2)) {
    zxlogf(ERROR, "Unsupported board revision %u. Booting will not continue", info.board_revision);
    return -1;
  }

  if ((status = RegistersInit()) != ZX_OK) {
    zxlogf(ERROR, "RegistersInit failed: %d", status);
  }

  if ((status = ClkInit()) != ZX_OK) {
    zxlogf(ERROR, "ClkInit failed: %d", status);
  }

  if ((status = ButtonsInit()) != ZX_OK) {
    zxlogf(ERROR, "ButtonsInit failed: %d", status);
  }

  if ((status = I2cInit()) != ZX_OK) {
    zxlogf(ERROR, "I2cInit failed: %d", status);
  }

  if ((status = CpuInit()) != ZX_OK) {
    zxlogf(ERROR, "CpuInit failed: %d", status);
  }

  if ((status = SpiInit()) != ZX_OK) {
    zxlogf(ERROR, "SpiInit failed: %d", status);
  }

  if ((status = SelinaInit()) != ZX_OK) {
    zxlogf(ERROR, "SelinaInit failed: %d\n", status);
  }

  if ((status = MaliInit()) != ZX_OK) {
    zxlogf(ERROR, "MaliInit failed: %d", status);
  }

  if ((status = UsbInit()) != ZX_OK) {
    zxlogf(ERROR, "UsbInit failed: %d", status);
  }

  if ((status = TouchInit()) != ZX_OK) {
    zxlogf(ERROR, "TouchInit failed: %d", status);
  }

  if ((status = DsiInit()) != ZX_OK) {
    zxlogf(ERROR, "DsiInit failed: %d", status);
  }

  if ((status = DisplayInit()) != ZX_OK) {
    zxlogf(ERROR, "DisplayInit failed: %d", status);
  }

  if ((status = CanvasInit()) != ZX_OK) {
    zxlogf(ERROR, "CanvasInit failed: %d", status);
  }

  if ((status = PwmInit()) != ZX_OK) {
    zxlogf(ERROR, "PwmInit failed: %d", status);
  }

  if ((status = TeeInit()) != ZX_OK) {
    zxlogf(ERROR, "TeeInit failed: %d", status);
  }

  if ((status = VideoInit()) != ZX_OK) {
    zxlogf(ERROR, "VideoInit failed: %d", status);
  }

  if ((status = pbus_.DeviceAdd(&rtc_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed - RTC: %d", __func__, status);
  }

  if ((status = EmmcInit()) != ZX_OK) {
    zxlogf(ERROR, "EmmcInit() failed: %d", status);
  }

  if ((status = SdioInit()) != ZX_OK) {
    zxlogf(ERROR, "SdioInit failed: %d", status);
  }

  if ((status = LightInit()) != ZX_OK) {
    zxlogf(ERROR, "LightInit failed: %d", status);
  }

  if ((status = ThermalInit()) != ZX_OK) {
    zxlogf(ERROR, "ThermalInit failed: %d", status);
  }

  if ((status = AudioInit()) != ZX_OK) {
    zxlogf(ERROR, "AudioInit failed: %d", status);
  }

  if ((status = SecureMemInit()) != ZX_OK) {
    zxlogf(ERROR, "SecureMemInit failed: %d", status);
  }

  if ((status = BacklightInit()) != ZX_OK) {
    zxlogf(ERROR, "BacklightInit failed: %d", status);
  }

  if ((status = PowerInit()) != ZX_OK) {
    zxlogf(ERROR, "PowerInit failed: %d", status);
  }

  if ((status = NnaInit()) != ZX_OK) {
    zxlogf(ERROR, "NnaInit failed: %d", status);
  }

  if (RamCtlInit() != ZX_OK) {
    zxlogf(ERROR, "RamCtlInit failed");
  }

  // This function includes some non-trivial delays, so lets run this last
  // to avoid slowing down the rest of the boot.
  if ((status = BluetoothInit()) != ZX_OK) {
    zxlogf(ERROR, "BluetoothInit failed: %d", status);
  }

  return ZX_OK;
}

zx_status_t Nelson::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Nelson*>(arg)->Thread(); }, this,
      "nelson-start-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Nelson::DdkRelease() { delete this; }

zx_status_t Nelson::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  iommu_protocol_t iommu;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Nelson>(&ac, parent, &pbus, &iommu);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("nelson", DEVICE_ADD_NON_BINDABLE);
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

static zx_driver_ops_t nelson_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Nelson::Create;
  return ops;
}();

}  // namespace nelson

ZIRCON_DRIVER(nelson, nelson::nelson_driver_ops, "zircon", "0.1");
