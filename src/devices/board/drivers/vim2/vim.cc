// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/drivers/vim2/vim.h"

#include <assert.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/hw/reg.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <soc/aml-s912/s912-hw.h>

#include "src/devices/board/drivers/vim2/vim2-bind.h"

namespace vim {

// TODO(rjascani): Remove this when not needed for testing any longer
const pbus_dev_t tee_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "tee";
  dev.vid = PDEV_VID_GENERIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_OPTEE;
  return dev;
}();

zx_status_t Vim::Create(void* ctx, zx_device_t* parent) {
  pbus_protocol_t pbus;
  iommu_protocol_t iommu;
  pbus_board_info_t info = {};

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    return status;
  }

  // Set dummy board revision to facilitate testing of platform device get_board_info support.
  info.board_revision = 1234;
  pbus_set_board_info(&pbus, &info);

  status = device_get_protocol(parent, ZX_PROTOCOL_IOMMU, &iommu);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto board = std::unique_ptr<Vim>(new (&ac) Vim(parent, &pbus, &iommu));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = board->DdkAdd("vim", DEVICE_ADD_NON_BINDABLE);
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

int Vim::Thread() {
  zx_status_t status;
  pdev_board_info_t info;

  // Fetch the board info so that we can distinguish between the "vim2" and
  // "vim2-machina" boards. The latter of which does not initialize devices.
  status = pbus_.GetBoardInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Thread: GetBoardInfo failed: %d", status);
    return -1;
  }

  if (info.pid == PDEV_PID_VIM2_MACHINA) {
    return ZX_OK;
  }

  // Start protocol drivers before adding platform devices.
  // Sysmem is started early so zx_vmo_create_contiguous() works.
  if ((status = SysmemInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: SysmemInit failed: %d", status);
    return -1;
  }

  if ((status = GpioInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: GpioInit failed: %d", status);
    return -1;
  }

  if ((status = I2cInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: I2cInit failed: %d", status);
    return -1;
  }

  if ((status = RegistersInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: RegistersInit failed: %d", status);
    return -1;
  }

  if ((status = ClkInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: ClkInit failed: %d", status);
    return -1;
  }

  if ((status = CanvasInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: CanvasInit failed: %d", status);
    return -1;
  }

  // Start platform devices.
  if ((status = UartInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: UartInit failed: %d", status);
    return -1;
  }

  if ((status = EmmcInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: EmmcInit failed: %d", status);
    return -1;
  }

  if ((status = SdioInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: SdioInit failed: %d", status);
    return -1;
  }

  if ((status = EthInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: EthInit failed: %d", status);
    return -1;
  }

  if ((status = UsbInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: UsbInit failed: %d", status);
    return -1;
  }

  if ((status = MaliInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: MaliInit failed: %d", status);
    return -1;
  }

  if ((status = ThermalInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: ThermalInit failed: %d", status);
    return -1;
  }

  if ((status = DisplayInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: DisplayInit failed: %d", status);
    return -1;
  }

  if ((status = VideoInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: VideoInit failed: %d", status);
    return -1;
  }

  // Remove this when not needed for testing any longer
  if ((status = pbus_.DeviceAdd(&tee_dev)) != ZX_OK) {
    zxlogf(ERROR, "vim_start_thread, could not add tee_dev: %d", status);
    return -1;
  }

  if ((status = SdInit()) != ZX_OK) {
    zxlogf(ERROR, "Thread: SdInit failed: %d", status);
    return -1;
  }

  return ZX_OK;
}

zx_status_t Vim::Start() {
  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Vim*>(arg)->Thread(); }, this,
      "vim-start-thread");

  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

void Vim::DdkRelease() { delete this; }
zx_driver_ops_t vim_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Vim::Create;
  return ops;
}();

}  // namespace vim

ZIRCON_DRIVER(vim, vim::vim_driver_ops, "zircon", "0.1");
