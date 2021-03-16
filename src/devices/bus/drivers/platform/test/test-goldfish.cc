// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::GoldfishAddressSpaceInit() {
  pbus_dev_t goldfish_address_space_dev = {};
  goldfish_address_space_dev.name = "goldfish_address_space";
  goldfish_address_space_dev.vid = PDEV_VID_TEST;
  goldfish_address_space_dev.pid = PDEV_PID_PBUS_TEST;
  goldfish_address_space_dev.did = PDEV_DID_TEST_GOLDFISH_ADDRESS_SPACE;

  zx_status_t status = pbus_.DeviceAdd(&goldfish_address_space_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t TestBoard::GoldfishPipeInit() {
  pbus_dev_t goldfish_pipe_dev = {};
  goldfish_pipe_dev.name = "goldfish_pipe";
  goldfish_pipe_dev.vid = PDEV_VID_TEST;
  goldfish_pipe_dev.pid = PDEV_PID_PBUS_TEST;
  goldfish_pipe_dev.did = PDEV_DID_TEST_GOLDFISH_PIPE;

  zx_status_t status = pbus_.DeviceAdd(&goldfish_pipe_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t TestBoard::GoldfishSyncInit() {
  pbus_dev_t goldfish_sync_dev = {};
  goldfish_sync_dev.name = "goldfish_sync";
  goldfish_sync_dev.vid = PDEV_VID_TEST;
  goldfish_sync_dev.pid = PDEV_PID_PBUS_TEST;
  goldfish_sync_dev.did = PDEV_DID_TEST_GOLDFISH_SYNC;

  zx_status_t status = pbus_.DeviceAdd(&goldfish_sync_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
