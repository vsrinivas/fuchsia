// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <fbl/algorithm.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>

#include "as370.h"
#include "src/devices/board/drivers/as370/as370-wifi-bind.h"
#include "src/devices/lib/nxp/include/wifi/wifi-config.h"

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

  static const wlan::nxpfmac::NxpSdioWifiConfig wifi_config = {
      .client_support = true,
      .softap_support = true,
      .sdio_rx_aggr_enable = true,
      .fixed_beacon_buffer = false,
      .auto_ds = true,
      .ps_mode = false,
      .max_tx_buf = 2048,
      .cfg_11d = false,
      .inact_tmo = false,
      .hs_wake_interval = 400,
      .indication_gpio = 0xff,
  };

  static const pbus_metadata_t sd_emmc_metadata[] = {
      {
          .type = DEVICE_METADATA_WIFI_CONFIG,
          .data_buffer = reinterpret_cast<const uint8_t*>(&wifi_config),
          .data_size = sizeof(wifi_config),
      },
  };

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "as370-sdio";
  sdio_dev.vid = PDEV_VID_SYNAPTICS;
  sdio_dev.pid = PDEV_PID_SYNAPTICS_AS370;
  sdio_dev.did = PDEV_DID_AS370_SDHCI0;
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = std::size(sdio_irqs);
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = std::size(sdio_mmios);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = std::size(sdio_btis);
  sdio_dev.metadata_list = sd_emmc_metadata;
  sdio_dev.metadata_count = std::size(sd_emmc_metadata);

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
    zxlogf(ERROR, "%s: SDIO Power/WLAN Enable error: %d", __func__, status);
  }

  status = pbus_.DeviceAdd(&sdio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd() error: %d", __func__, status);
  }

  constexpr zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_NXP},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_MARVELL_88W8987},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_MARVELL_WIFI},
  };

  const composite_device_desc_t comp_desc = {
      .props = props,
      .props_count = std::size(props),
      .fragments = wifi_fragments,
      .fragments_count = std::size(wifi_fragments),
      .primary_fragment = "sdio-function-1",
      .spawn_colocated = true,
      .metadata_list = nullptr,
      .metadata_count = 0,
  };

  status = DdkAddComposite("wifi", &comp_desc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAddComposite failed; %s", __func__, zx_status_get_string(status));
  }

  return status;
}

}  // namespace board_as370
