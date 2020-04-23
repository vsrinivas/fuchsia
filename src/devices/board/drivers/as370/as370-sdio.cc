// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fbl/algorithm.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::SdioInit() {
  zx_status_t status;

  constexpr pbus_mmio_t sdio_mmios[] = {
      {
          .base = as370::kSdio0Base,
          .length = as370::kSdio0Size,
      },
  };

  constexpr pbus_irq_t sdio_irqs[] = {
      {
          .irq = as370::kSdio0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr pbus_bti_t sdio_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_SDIO0,
      },
  };

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "as370-sdio";
  sdio_dev.vid = PDEV_VID_SYNAPTICS;
  sdio_dev.pid = PDEV_PID_SYNAPTICS_AS370;
  sdio_dev.did = PDEV_DID_AS370_SDHCI0;
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = countof(sdio_irqs);
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = countof(sdio_mmios);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = countof(sdio_btis);

  // Configure eMMC-SD soc pads.
  if (((status = gpio_impl_.SetAltFunction(58, 1)) != ZX_OK) ||  // SD0_CLK
      ((status = gpio_impl_.SetAltFunction(61, 1)) != ZX_OK) ||  // SD0_CMD
      ((status = gpio_impl_.SetAltFunction(56, 1)) != ZX_OK) ||  // SD0_DAT0
      ((status = gpio_impl_.SetAltFunction(57, 1)) != ZX_OK) ||  // SD0_DAT1
      ((status = gpio_impl_.SetAltFunction(59, 1)) != ZX_OK) ||  // SD0_DAT2
      ((status = gpio_impl_.SetAltFunction(60, 1)) != ZX_OK) ||  // SD0_DAT3
      ((status = gpio_impl_.SetAltFunction(62, 1)) != ZX_OK) ||  // SD0_CDn
      ((status = gpio_impl_.SetAltFunction(63, 0)) != ZX_OK)) {  // SDIO_PWR_EN | WLAN_EN
    return status;
  }

  status = gpio_impl_.ConfigOut(63, 1);  // Disable WLAN Powerdown
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SDIO Power/WLAN Enable error: %d\n", __func__, status);
  }

  status = pbus_.DeviceAdd(&sdio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd() error: %d\n", __func__, status);
  }

  return status;
}

}  // namespace board_as370
