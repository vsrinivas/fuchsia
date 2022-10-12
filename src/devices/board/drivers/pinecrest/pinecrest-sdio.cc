// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpioimpl/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <fbl/algorithm.h>
#include <soc/as370/as370-gpio.h>
#include <soc/as370/as370-hw.h>

#include "pinecrest.h"
#include "src/devices/board/drivers/pinecrest/pinecrest-wifi-bind.h"
#include "src/devices/lib/nxp/include/wifi/wifi-config.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::SdioInit() {
  zx_status_t status;

  static const std::vector<fpbus::Mmio> sdio_mmios{
      {{
          .base = as370::kSdio0Base,
          .length = as370::kSdio0Size,
      }},
  };

  static const std::vector<fpbus::Irq> sdio_irqs{
      {{
          .irq = as370::kSdio0Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
  };

  static const std::vector<fpbus::Bti> sdio_btis{
      {{
          .iommu_index = 0,
          .bti_id = BTI_SDIO0,
      }},
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

  static const std::vector<fpbus::Metadata> sd_emmc_metadata = {
      {{
          .type = DEVICE_METADATA_WIFI_CONFIG,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&wifi_config),
              reinterpret_cast<const uint8_t*>(&wifi_config) + sizeof(wifi_config)),
      }},
  };

  fpbus::Node sdio_dev;
  sdio_dev.name() = "pinecrest-sdio";
  sdio_dev.vid() = PDEV_VID_SYNAPTICS;
  sdio_dev.pid() = PDEV_PID_SYNAPTICS_AS370;
  sdio_dev.did() = PDEV_DID_AS370_SDHCI0;
  sdio_dev.irq() = sdio_irqs;
  sdio_dev.mmio() = sdio_mmios;
  sdio_dev.bti() = sdio_btis;
  sdio_dev.metadata() = sd_emmc_metadata;

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

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('SDIO');
  auto result = pbus_.buffer(arena)->NodeAdd(fidl::ToWire(fidl_arena, sdio_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: NodeAdd Sdio(sdio_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: NodeAdd Sdio(sdio_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
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

}  // namespace board_pinecrest
