// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include "test.h"

namespace board_test {

zx_status_t TestBoard::PciInit() {
  pbus_dev_t pci_dev{};
  pci_dev.name = "pci";
  pci_dev.vid = PDEV_VID_TEST;
  pci_dev.pid = PDEV_PID_PBUS_TEST;
  pci_dev.did = PDEV_DID_TEST_PCI;

  zx_status_t status = pbus_.DeviceAdd(&pci_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_test
