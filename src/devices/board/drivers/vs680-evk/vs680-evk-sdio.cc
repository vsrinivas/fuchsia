// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2cimpl.h>
#include <fbl/algorithm.h>
#include <soc/vs680/vs680-clk.h>
#include <soc/vs680/vs680-hw.h>

#include "src/devices/board/drivers/vs680-evk/vs680-evk-bind.h"
#include "vs680-evk.h"

namespace board_vs680_evk {

zx_status_t Vs680Evk::SdioInit() {
  constexpr zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };

  constexpr zx_bind_inst_t expander2_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x43),
  };
  constexpr zx_bind_inst_t expander3_i2c_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
      BI_ABORT_IF(NE, BIND_I2C_BUS_ID, 0),
      BI_MATCH_IF(EQ, BIND_I2C_ADDRESS, 0x44),
  };
  constexpr zx_bind_inst_t sd0_clock_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_CLOCK),
      BI_MATCH_IF(EQ, BIND_CLOCK_ID, vs680::kSd0Clock),
  };

  const device_fragment_part_t expander2_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(expander2_i2c_match), expander2_i2c_match},
  };
  const device_fragment_part_t expander3_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(expander3_i2c_match), expander3_i2c_match},
  };
  const device_fragment_part_t sd0_clock_fragment[] = {
      {std::size(root_match), root_match},
      {std::size(sd0_clock_match), sd0_clock_match},
  };

  const device_fragment_t sdio_fragments[] = {
      {"i2c-expander-2", std::size(expander2_fragment), expander2_fragment},
      {"i2c-expander-3", std::size(expander3_fragment), expander3_fragment},
      {"clock-sd-0", std::size(sd0_clock_fragment), sd0_clock_fragment},
  };

  constexpr pbus_mmio_t sdio_mmios[] = {
      {
          .base = vs680::kSdioBase,
          .length = vs680::kSdioSize,
      },
      {
          .base = vs680::kChipCtrlBase,
          .length = vs680::kChipCtrlSize,
      },
  };

  constexpr pbus_irq_t sdio_irqs[] = {
      {
          .irq = vs680::kSdioIrq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  constexpr pbus_bti_t sdio_btis[] = {
      {
          .iommu_index = 0,
          .bti_id = BTI_SDIO,
      },
  };

  pbus_dev_t sdio_dev = {};
  sdio_dev.name = "vs680-sdio";
  sdio_dev.vid = PDEV_VID_SYNAPTICS;
  sdio_dev.pid = PDEV_PID_SYNAPTICS_VS680;
  sdio_dev.did = PDEV_DID_VS680_SDHCI1;
  sdio_dev.irq_list = sdio_irqs;
  sdio_dev.irq_count = countof(sdio_irqs);
  sdio_dev.mmio_list = sdio_mmios;
  sdio_dev.mmio_count = countof(sdio_mmios);
  sdio_dev.bti_list = sdio_btis;
  sdio_dev.bti_count = countof(sdio_btis);

  zx_status_t status =
      pbus_.CompositeDeviceAdd(&sdio_dev, sdio_fragments, std::size(sdio_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd() error: %d", __func__, status);
  }

  return status;
}

}  // namespace board_vs680_evk
