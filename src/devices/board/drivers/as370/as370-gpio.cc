// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>

#include <lib/ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <soc/as370/as370-gpio.h>
#include <soc/synaptics/gpio.h>

#include "as370.h"

namespace board_as370 {

zx_status_t As370::GpioInit() {
  synaptics::PinmuxMetadata
      pinmux_metadata =
          {
              .muxes = 1,
              .pinmux_map =
                  {
                      [0] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              0,
                          },  // I2S1_BCLKIO
                      [1] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              1,
                          },  // I2S1_LRCKIO
                      [2] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              2,
                          },  // I2S1_DO0
                      [3] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              3,
                          },  // I2S1_DO1
                      [4] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              4,
                          },  // I2S1_DO2
                      [5] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              5,
                          },  // I2S1_DO3
                      [6] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              6,
                          },  // I2S1_MCLK
                      [7] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              7,
                          },  // I2S2_BCLKIO
                      [8] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              8,
                          },  // I2S2_LRCKIO
                      [9] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              9,
                          },  // I2S2_DI0
                      [10] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              10,
                          },  // I2S2_DI1
                      [11] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              11,
                          },  // I2S2_DI2
                      [12] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              12,
                          },  // I2S2_DI3
                      [13] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              13,
                          },  // PDM_CLKO
                      [14] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              14,
                          },  // PDM_DI0
                      [15] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              15,
                          },  // PDM_DI1
                      [16] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              16,
                          },  // PDM_DI2
                      [17] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              17,
                          },  // PDM_DI3
                      [18] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              26,
                          },  // NAND_ALE
                      [19] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              27,
                          },  // NAND_CLE
                      [20] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              28,
                          },  // NAND_WEn
                      [21] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              29,
                          },  // NAND_REn
                      [22] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              30,
                          },  // NAND_WPn
                      [23] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              31,
                          },  // NAND_CEn
                      [24] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              32,
                          },  // NAND_RDY
                      [25] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              33,
                          },  // SPI1_SS0n
                      [26] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              34,
                          },  // SPI1_SS1n
                      [27] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              35,
                          },  // SPI1_SS2n
                      [28] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              36,
                          },  // SPI1_SS3n
                      [29] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              37,
                          },  // SPI1_SCLK
                      [30] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              38,
                          },  // SPI1_SDO
                      [31] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              39,
                          },  // SPI1_SDI
                      [32] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              40,
                          },  // USB0_DRV_VBUS
                      [33] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              41,
                          },  // TW1_SCL
                      [34] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              42,
                          },  // TW1_SDA
                      [35] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              43,
                          },  // TW0_SCL
                      [36] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              44,
                          },  // TW0_SDA
                      [37] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              45,
                          },  // TMS
                      [38] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              46,
                          },  // TDI
                      [39] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              47,
                          },  // TDO
                      [40] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              48,
                          },  // PWM6
                      [41] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              49,
                          },  // PWM7
                      [42] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              50,
                          },  // PWM0
                      [43] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              51,
                          },  // PWM1
                      [44] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              52,
                          },  // PWM2
                      [45] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              53,
                          },  // PWM3
                      [46] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              54,
                          },  // PWM4
                      [47] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              55,
                          },  // PWM5
                      [48] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              56,
                          },  // URT1_RTSn
                      [49] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              57,
                          },  // URT1_CTSn
                      [50] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              58,
                          },  // URT1_RXD
                      [51] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              59,
                          },  // URT1_TXD
                      [52] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              60,
                          },  // I2S3_DI
                      [53] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              61,
                          },  // I2S3_DO
                      [54] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              62,
                          },  // I2S3_BCLKIO
                      [55] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              63,
                          },  // I2S3_LRCKIO
                      [56] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              64,
                          },  // SD0_DAT0
                      [57] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              65,
                          },  // SD0_DAT1
                      [58] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              66,
                          },  // SD0_CLK
                      [59] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              67,
                          },  // SD0_DAT2
                      [60] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              68,
                          },  // SD0_DAT3
                      [61] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              69,
                          },  // SD0_CMD
                      [62] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              70,
                          },  // SD0_CDn
                      [63] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              71,
                          },  // SD0_WP
                      [64] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              18,
                          },  // NAND_IO0
                      [65] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              19,
                          },  // NAND_IO1
                      [66] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              20,
                          },  // NAND_IO2
                      [67] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              21,
                          },  // NAND_IO3
                      [68] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              22,
                          },  // NAND_IO4
                      [69] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              23,
                          },  // NAND_IO5
                      [70] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              24,
                          },  // NAND_IO6
                      [71] =
                          {
                              synaptics::PinmuxEntry::kMuxOnly,
                              0,
                              25,
                          },  // NAND_IO7
                  },
          };

  constexpr pbus_mmio_t gpio_mmios[] = {
      {.base = as370::kPinmuxBase, .length = as370::kPinmuxSize},
      {.base = as370::kGpio1Base, .length = as370::kGpioSize},
      {.base = as370::kGpio2Base, .length = as370::kGpioSize},
  };

  const pbus_irq_t gpio_irqs[] = {
      {
          .irq = as370::kGpio1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  const gpio_pin_t gpio_pins[] = {
      {17},  // AMP_EN.
      {4},   // LED_RESET / TOUCH_RESET
      {5},   // TOUCH_IRQ
      {63},  // WLAN_EN
  };

  const pbus_metadata_t gpio_metadata[] = {
      {
          .type = DEVICE_METADATA_GPIO_PINS,
          .data_buffer = reinterpret_cast<const uint8_t*>(&gpio_pins),
          .data_size = sizeof(gpio_pins),
      },
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = reinterpret_cast<const uint8_t*>(&pinmux_metadata),
          .data_size = sizeof(pinmux_metadata),
      },
  };

  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_SYNAPTICS;
  gpio_dev.pid = PDEV_PID_SYNAPTICS_AS370;
  gpio_dev.did = PDEV_DID_SYNAPTICS_GPIO;
  gpio_dev.mmio_list = gpio_mmios;
  gpio_dev.mmio_count = countof(gpio_mmios);
  gpio_dev.irq_list = gpio_irqs;
  gpio_dev.irq_count = countof(gpio_irqs);
  gpio_dev.metadata_list = gpio_metadata;
  gpio_dev.metadata_count = countof(gpio_metadata);

  auto status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ProtocolDeviceAdd failed: %d", __func__, status);
    return status;
  }
  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed", __func__);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

}  // namespace board_as370
