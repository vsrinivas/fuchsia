// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/gpio.h>
#include <ddk/platform-defs.h>
#include <soc/synaptics/gpio.h>
#include <soc/vs680/vs680-gpio.h>

#include "luis.h"

namespace board_luis {

zx_status_t Luis::GpioInit() {
  constexpr synaptics::PinmuxMetadata
      pinmux_metadata =
          {
              .muxes = 3,
              .pinmux_map =
                  {
                      // AVIO GPIOs
                      [0] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              21,
                          },  // I2S3_DI
                      [1] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              18,
                          },  // I2S3_DO
                      [2] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              20,
                          },  // I2S3_BCLKIO
                      [3] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              19,
                          },  // I2S3_LRCKIO
                      [4] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              7,
                          },  // SPDIFI
                      [5] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              17,
                          },  // TX_EDDC_SDA
                      [6] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              16,
                          },  // TX_EDDC_SCL
                      [7] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              15,
                          },  // I2S2_MCLK
                      [8] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              13,
                          },  // I2S2_DI3
                      [9] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              12,
                          },  // I2S2_DI2
                      [10] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              11,
                          },  // I2S2_DI1
                      [11] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              10,
                          },  // I2S2_DI0
                      [12] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              9,
                          },  // I2S2_BCLKIO
                      [13] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              8,
                          },  // I2S2_LRCKIO
                      [14] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              6,
                          },  // SPDIFO
                      [15] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              3,
                          },  // I2S1_DO3
                      [16] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              2,
                          },  // I2S1_DO2
                      [17] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              1,
                          },  // I2S1_DO1
                      [18] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              14,
                          },  // I2S1_MCLK
                      [19] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              0,
                          },  // I2S1_DO0
                      [20] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              5,
                          },  // I2S1_BCLKIO
                      [21] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              1,
                              4,
                          },  // I2S1_LRCKIO

                      // SoC GPIOs
                      [22] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              27,
                          },  // RGMII_TXCTL
                      [23] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              22,
                          },  // RGMII_TXC
                      [24] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              26,
                          },  // RGMII_TXD3
                      [25] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              25,
                          },  // RGMII_TXD2
                      [26] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              24,
                          },  // RGMII_TXD1
                      [27] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              23,
                          },  // RGMII_TXD0
                      [28] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              21,
                          },  // RGMII_MDIO
                      [29] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              20,
                          },  // RGMII_MDC
                      [30] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              33,
                          },  // RGMII_RXCTL
                      [31] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              28,
                          },  // RGMII_RXC
                      [32] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              32,
                          },  // RGMII_RXD3
                      [33] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              31,
                          },  // RGMII_RXD2
                      [34] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              30,
                          },  // RGMII_RXD1
                      [35] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              29,
                          },  // RGMII_RXD0
                      [36] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              18,
                          },  // STS1_VALD
                      [37] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              17,
                          },  // STS1_SD
                      [38] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              16,
                          },  // STS1_SOP
                      [39] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              15,
                          },  // STS1_CLK
                      [40] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              14,
                          },  // STS0_VALD
                      [41] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              13,
                          },  // STS0_SD
                      [42] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              12,
                          },  // STS0_SOP
                      [43] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              11,
                          },  // STS0_CLK
                      [44] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              1,
                          },  // SDIO_WP
                      [45] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              0,
                          },  // SDIO_CDn
                      [46] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              10,
                          },  // TW0_SDA
                      [47] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              9,
                          },  // TW0_SCL
                      [48] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              8,
                          },  // SPI1_SDI
                      [49] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              7,
                          },  // SPI1_SCLK
                      [50] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              6,
                          },  // SPI1_SDO
                      [51] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              5,
                          },  // SPI1_SS3n
                      [52] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              4,
                          },  // SPI1_SS2n
                      [53] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              3,
                          },  // SPI1_SS1n
                      [54] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              2,
                          },  // SPI1_SS0n
                      [55] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              0,
                              19,
                          },  // USB2_DRV_VBUS

                      // System manager GPIOs
                      [64] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              0,
                          },  // SM_TW2_SCL
                      [65] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              1,
                          },  // SM_TW2_SDA
                      [66] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              4,
                          },  // SM_HDMI_HPD
                      [67] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              5,
                          },  // SM_HDMI_CEC
                      [68] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              2,
                          },  // SM_URT1_TXD
                      [69] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              3,
                          },  // SM_URT1_RXD
                      [70] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              6,
                          },  // SM_TMS
                      [71] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              7,
                          },  // SM_TDI
                      [72] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              8,
                          },  // SM_TDO
                      [73] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              9,
                          },  // SM_TW3_SCL
                      [74] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              10,
                          },  // SM_TW3_SDA
                      [75] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              17,
                          },  // SM_SPI2_SCLK
                      [76] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              16,
                          },  // SM_SPI2_SDI
                      [77] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              15,
                          },  // SM_SPI2_SDO
                      [78] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              14,
                          },  // SM_SPI2_SS3n
                      [79] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              13,
                          },  // SM_SPI2_SS2n
                      [80] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              12,
                          },  // SM_SPI2_SS1n
                      [81] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              11,
                          },  // SM_SPI2_SS0n
                      [82] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              18,
                          },  // SM_URT0_TXD
                      [83] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              19,
                          },  // SM_URT0_RXD
                      [84] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              20,
                          },  // SM_HDMIRX_HPD
                      [85] =
                          {
                              synaptics::PinmuxEntry::kGpio,
                              2,
                              21,
                          },  // SM_HDMIRX_PWR5V
                  },
          };

  constexpr pbus_mmio_t gpio_mmios[] = {
      {.base = vs680::kSocPinmuxBase, .length = vs680::kPinmuxSize},
      {.base = vs680::kAvioPinmuxBase, .length = vs680::kPinmuxSize},
      {.base = vs680::kSmPinmuxBase, .length = vs680::kPinmuxSize},
      {.base = vs680::kGpio1Base, .length = vs680::kGpioSize},
      {.base = vs680::kGpio2Base, .length = vs680::kGpioSize},
      {.base = vs680::kSmGpioBase, .length = vs680::kGpioSize},
  };

  const pbus_irq_t gpio_irqs[] = {
      {
          .irq = vs680::kGpio1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
      {
          .irq = vs680::kGpio2Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      },
  };

  const gpio_pin_t gpio_pins[] = {};

  const pbus_metadata_t gpio_metadata[] = {
      {
          .type = DEVICE_METADATA_GPIO_PINS,
          .data_buffer = &gpio_pins,
          .data_size = sizeof(gpio_pins),
      },
      {
          .type = DEVICE_METADATA_PRIVATE,
          .data_buffer = &pinmux_metadata,
          .data_size = sizeof(pinmux_metadata),
      },
  };

  pbus_dev_t gpio_dev = {};
  gpio_dev.name = "gpio";
  gpio_dev.vid = PDEV_VID_SYNAPTICS;
  gpio_dev.pid = PDEV_PID_SYNAPTICS_VS680;
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

}  // namespace board_luis
