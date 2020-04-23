// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <soc/mt8167/mt8167-gpio.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::I2cInit() {
  static const zx_bind_inst_t root_match[] = {
      BI_MATCH(),
  };
  static const zx_bind_inst_t sda0_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO58_SDA0),
  };
  static const zx_bind_inst_t scl0_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO59_SCL0),
  };
  static const zx_bind_inst_t sda1_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO52_SDA1),
  };
  static const zx_bind_inst_t scl1_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO53_SCL1),
  };
  static const zx_bind_inst_t sda2_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO60_SDA2),
  };
  static const zx_bind_inst_t scl2_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_GPIO),
      BI_MATCH_IF(EQ, BIND_GPIO_PIN, MT8167_GPIO61_SCL2),
  };
  static const device_fragment_part_t sda0_fragment[] = {
      {countof(root_match), root_match},
      {countof(sda0_match), sda0_match},
  };
  static const device_fragment_part_t scl0_fragment[] = {
      {countof(root_match), root_match},
      {countof(scl0_match), scl0_match},
  };
  static const device_fragment_part_t sda1_fragment[] = {
      {countof(root_match), root_match},
      {countof(sda1_match), sda1_match},
  };
  static const device_fragment_part_t scl1_fragment[] = {
      {countof(root_match), root_match},
      {countof(scl1_match), scl1_match},
  };
  static const device_fragment_part_t sda2_fragment[] = {
      {countof(root_match), root_match},
      {countof(sda2_match), sda2_match},
  };
  static const device_fragment_part_t scl2_fragment[] = {
      {countof(root_match), root_match},
      {countof(scl2_match), scl2_match},
  };
  static const device_fragment_t fragments[] = {
      {countof(sda0_fragment), sda0_fragment}, {countof(scl0_fragment), scl0_fragment},
      {countof(sda1_fragment), sda1_fragment}, {countof(scl1_fragment), scl1_fragment},
      {countof(sda2_fragment), sda2_fragment}, {countof(scl2_fragment), scl2_fragment},
  };
  constexpr pbus_mmio_t i2c_mmios[] = {
      {
          .base = MT8167_I2C0_BASE,
          .length = MT8167_I2C0_SIZE,
      },
      {
          .base = MT8167_I2C1_BASE,
          .length = MT8167_I2C1_SIZE,
      },
      {
          .base = MT8167_I2C2_BASE,
          .length = MT8167_I2C2_SIZE,
      },
      // MMIO for clocks.
      // TODO(andresoportus): Move this to a clock driver.
      {
          .base = MT8167_XO_BASE,
          .length = MT8167_XO_SIZE,
      },
  };

  constexpr pbus_irq_t i2c_irqs[] = {
      {
          .irq = MT8167_IRQ_I2C0,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
      {
          .irq = MT8167_IRQ_I2C1,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
      {
          .irq = MT8167_IRQ_I2C2,
          .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
      },
  };

  constexpr i2c_channel_t mt8167s_i2c_channels[] = {
      // For mt8167s_ref audio out
      {
          .bus_id = 2,
          .address = 0x48,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For audio in
      {
          .bus_id = 1,
          .address = 0x1B,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  constexpr i2c_channel_t cleo_i2c_channels[] = {
      {
          .bus_id = 0,
          .address = 0x53,
          .vid = PDEV_VID_GENERIC,
          .pid = PDEV_PID_GENERIC,
          .did = PDEV_DID_LITE_ON_ALS,
      },
      {
          .bus_id = 0,
          .address = 0x18,
          .vid = PDEV_VID_GENERIC,
          .pid = PDEV_PID_GENERIC,
          .did = PDEV_DID_BOSCH_BMA253,
      },
      // For backlight driver
      {
          .bus_id = 2,
          .address = 0x36,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For touch screen driver
      {
          .bus_id = 0,
          .address = 0x38,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For cleo audio out
      {
          .bus_id = 2,
          .address = 0x2C,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
      // For audio in
      {
          .bus_id = 1,
          .address = 0x1B,
          .vid = 0,
          .pid = 0,
          .did = 0,
      },
  };

  const pbus_metadata_t mt8167s_i2c_metadata[] = {
      {
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data_buffer = &mt8167s_i2c_channels,
          .data_size = sizeof(mt8167s_i2c_channels),
      },
  };
  const pbus_metadata_t cleo_i2c_metadata[] = {
      {
          .type = DEVICE_METADATA_I2C_CHANNELS,
          .data_buffer = &cleo_i2c_channels,
          .data_size = sizeof(cleo_i2c_channels),
      },
  };

  pbus_dev_t i2c_dev = {};
  i2c_dev.name = "mt8167-i2c";
  i2c_dev.vid = PDEV_VID_MEDIATEK;
  i2c_dev.did = PDEV_DID_MEDIATEK_I2C;
  i2c_dev.mmio_list = i2c_mmios;
  i2c_dev.mmio_count = countof(i2c_mmios);
  i2c_dev.irq_list = i2c_irqs;
  i2c_dev.irq_count = countof(i2c_irqs);

  if (board_info_.vid == PDEV_VID_GOOGLE && board_info_.pid == PDEV_PID_CLEO) {
    i2c_dev.metadata_list = cleo_i2c_metadata;
    i2c_dev.metadata_count = countof(cleo_i2c_metadata);
  } else if (board_info_.vid == PDEV_VID_MEDIATEK &&
             board_info_.pid == PDEV_PID_MEDIATEK_8167S_REF) {
    i2c_dev.metadata_list = mt8167s_i2c_metadata;
    i2c_dev.metadata_count = countof(mt8167s_i2c_metadata);
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto status = pbus_.CompositeDeviceAdd(&i2c_dev, fragments, countof(fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: CompositeDeviceAdd failed %d", __FUNCTION__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace board_mt8167
