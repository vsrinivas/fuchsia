// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nelson.h"

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
    zxlogf(INFO, "Unsupported board revision detected (%d)\n", board_rev);
  }

  return board_rev;
}

int Nelson::Thread() {
  zx_status_t status;

  // Sysmem is started early so zx_vmo_create_contiguous() works.
  if ((status = SysmemInit()) != ZX_OK) {
    zxlogf(ERROR, "%s: SysmemInit() failed: %d\n", __func__, status);
    return status;
  }

  if ((status = GpioInit()) != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit() failed: %d\n", __func__, status);
    return status;
  }

  // Once gpio is up and running, let's populate board revision
  pbus_board_info_t info = {};
  info.board_revision = GetBoardRev();
  status = pbus_.SetBoardInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: PBusSetBoardInfo failed: %d\n", __func__, status);
  }
  zxlogf(INFO, "Detected board rev 0x%x\n", info.board_revision);

  if ((info.board_revision != BOARD_REV_P1) && (info.board_revision != BOARD_REV_P2)) {
    zxlogf(ERROR, "Unsupported board revision %u. Booting will not continue\n",
           info.board_revision);
    return -1;
  }

  if ((status = ClkInit()) != ZX_OK) {
    zxlogf(ERROR, "ClkInit failed: %d\n", status);
  }

  if ((status = ButtonsInit()) != ZX_OK) {
    zxlogf(ERROR, "ButtonsInit failed: %d\n", status);
  }

  if ((status = I2cInit()) != ZX_OK) {
    zxlogf(ERROR, "I2cInit failed: %d\n", status);
  }

  if ((status = CpuInit()) != ZX_OK) {
    zxlogf(ERROR, "CpuInit failed: %d\n", status);
  }

  if ((status = SpiInit()) != ZX_OK) {
    zxlogf(ERROR, "SpiInit failed: %d\n", status);
  }

  if ((status = MaliInit()) != ZX_OK) {
    zxlogf(ERROR, "MaliInit failed: %d\n", status);
  }

  if ((status = UsbInit()) != ZX_OK) {
    zxlogf(ERROR, "UsbInit failed: %d\n", status);
  }

  // TODO(fxb/48099): Enable init once the touch driver has landed.
  // if ((status = TouchInit()) != ZX_OK) {
  //   zxlogf(ERROR, "TouchInit failed: %d\n", status);
  // }

  if ((status = DisplayInit()) != ZX_OK) {
    zxlogf(ERROR, "DisplayInit failed: %d\n", status);
  }

  if ((status = CanvasInit()) != ZX_OK) {
    zxlogf(ERROR, "CanvasInit failed: %d\n", status);
  }

  if ((status = PwmInit()) != ZX_OK) {
    zxlogf(ERROR, "PwmInit failed: %d\n", status);
  }

  if ((status = TeeInit()) != ZX_OK) {
    zxlogf(ERROR, "TeeInit failed: %d\n", status);
  }

  if ((status = VideoInit()) != ZX_OK) {
    zxlogf(ERROR, "VideoInit failed: %d\n", status);
  }

  if ((status = pbus_.DeviceAdd(&rtc_dev)) != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed - RTC: %d\n", __func__, status);
  }

  if ((status = EmmcInit()) != ZX_OK) {
    zxlogf(ERROR, "EmmcInit() failed: %d\n", status);
  }

  if ((status = SdioInit()) != ZX_OK) {
    zxlogf(ERROR, "SdioInit failed: %d\n", status);
  }

  if ((status = LightInit()) != ZX_OK) {
    zxlogf(ERROR, "LightInit failed: %d\n", status);
  }

  if ((status = ThermalInit()) != ZX_OK) {
    zxlogf(ERROR, "ThermalInit failed: %d\n", status);
  }

  if ((status = AudioInit()) != ZX_OK) {
    zxlogf(ERROR, "AudioInit failed: %d\n", status);
  }

  if ((status = SecureMemInit()) != ZX_OK) {
    zxlogf(ERROR, "SecureMemInit failed: %d\n", status);
  }

  if ((status = BacklightInit()) != ZX_OK) {
    zxlogf(ERROR, "BacklightInit failed: %d\n", status);
  }

  // This function includes some non-trivial delays, so lets run this last
  // to avoid slowing down the rest of the boot.
  if ((status = BluetoothInit()) != ZX_OK) {
    zxlogf(ERROR, "BluetoothInit failed: %d\n", status);
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

ZIRCON_DRIVER_BEGIN(nelson, nelson::nelson_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PBUS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_NELSON), ZIRCON_DRIVER_END(nelson)
