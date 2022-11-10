// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pinecrest-gpio.h"

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <ddk/metadata/gpio.h>
#include <soc/as370/as370-gpio.h>
#include <soc/synaptics/gpio.h>

#include "pinecrest.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::GpioInit() {
  synaptics::PinmuxMetadata pinmux_metadata = {};
  pinmux_metadata.muxes = 1;
  pinmux_metadata.pinmux_map[0] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      0,
  };  // I2S1_BCLKIO
  pinmux_metadata.pinmux_map[1] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      1,
  };  // I2S1_LRCKIO
  pinmux_metadata.pinmux_map[2] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      2,
  };  // I2S1_DO0
  pinmux_metadata.pinmux_map[3] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      3,
  };  // I2S1_DO1
  pinmux_metadata.pinmux_map[4] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      4,
  };  // I2S1_DO2
  pinmux_metadata.pinmux_map[5] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      5,
  };  // I2S1_DO3
  pinmux_metadata.pinmux_map[6] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      6,
  };  // I2S1_MCLK
  pinmux_metadata.pinmux_map[7] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      7,
  };  // I2S2_BCLKIO
  pinmux_metadata.pinmux_map[8] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      8,
  };  // I2S2_LRCKIO
  pinmux_metadata.pinmux_map[9] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      9,
  };  // I2S2_DI0
  pinmux_metadata.pinmux_map[10] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      10,
  };  // I2S2_DI1
  pinmux_metadata.pinmux_map[11] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      11,
  };  // I2S2_DI2
  pinmux_metadata.pinmux_map[12] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      12,
  };  // I2S2_DI3
  pinmux_metadata.pinmux_map[13] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      13,
  };  // PDM_CLKO
  pinmux_metadata.pinmux_map[14] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      14,
  };  // PDM_DI0
  pinmux_metadata.pinmux_map[15] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      15,
  };  // PDM_DI1
  pinmux_metadata.pinmux_map[16] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      16,
  };  // PDM_DI2
  pinmux_metadata.pinmux_map[17] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      17,
  };  // PDM_DI3
  pinmux_metadata.pinmux_map[18] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      26,
  };  // NAND_ALE
  pinmux_metadata.pinmux_map[19] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      27,
  };  // NAND_CLE
  pinmux_metadata.pinmux_map[20] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      28,
  };  // NAND_WEn
  pinmux_metadata.pinmux_map[21] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      29,
  };  // NAND_REn
  pinmux_metadata.pinmux_map[22] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      30,
  };  // NAND_WPn
  pinmux_metadata.pinmux_map[23] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      31,
  };  // NAND_CEn
  pinmux_metadata.pinmux_map[24] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      32,
  };  // NAND_RDY
  pinmux_metadata.pinmux_map[25] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      33,
  };  // SPI1_SS0n
  pinmux_metadata.pinmux_map[26] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      34,
  };  // SPI1_SS1n
  pinmux_metadata.pinmux_map[27] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      35,
  };  // SPI1_SS2n
  pinmux_metadata.pinmux_map[28] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      36,
  };  // SPI1_SS3n
  pinmux_metadata.pinmux_map[29] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      37,
  };  // SPI1_SCLK
  pinmux_metadata.pinmux_map[30] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      38,
  };  // SPI1_SDO
  pinmux_metadata.pinmux_map[31] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      39,
  };  // SPI1_SDI
  pinmux_metadata.pinmux_map[32] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      40,
  };  // USB0_DRV_VBUS
  pinmux_metadata.pinmux_map[33] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      41,
  };  // TW1_SCL
  pinmux_metadata.pinmux_map[34] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      42,
  };  // TW1_SDA
  pinmux_metadata.pinmux_map[35] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      43,
  };  // TW0_SCL
  pinmux_metadata.pinmux_map[36] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      44,
  };  // TW0_SDA
  pinmux_metadata.pinmux_map[37] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      45,
  };  // TMS
  pinmux_metadata.pinmux_map[38] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      46,
  };  // TDI
  pinmux_metadata.pinmux_map[39] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      47,
  };  // TDO
  pinmux_metadata.pinmux_map[40] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      48,
  };  // PWM6
  pinmux_metadata.pinmux_map[41] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      49,
  };  // PWM7
  pinmux_metadata.pinmux_map[42] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      50,
  };  // PWM0
  pinmux_metadata.pinmux_map[43] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      51,
  };  // PWM1
  pinmux_metadata.pinmux_map[44] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      52,
  };  // PWM2
  pinmux_metadata.pinmux_map[45] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      53,
  };  // PWM3
  pinmux_metadata.pinmux_map[46] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      54,
  };  // PWM4
  pinmux_metadata.pinmux_map[47] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      55,
  };  // PWM5
  pinmux_metadata.pinmux_map[48] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      56,
  };  // URT1_RTSn
  pinmux_metadata.pinmux_map[49] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      57,
  };  // URT1_CTSn
  pinmux_metadata.pinmux_map[50] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      58,
  };  // URT1_RXD
  pinmux_metadata.pinmux_map[51] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      59,
  };  // URT1_TXD
  pinmux_metadata.pinmux_map[52] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      60,
  };  // I2S3_DI
  pinmux_metadata.pinmux_map[53] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      61,
  };  // I2S3_DO
  pinmux_metadata.pinmux_map[54] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      62,
  };  // I2S3_BCLKIO
  pinmux_metadata.pinmux_map[55] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      63,
  };  // I2S3_LRCKIO
  pinmux_metadata.pinmux_map[56] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      64,
  };  // SD0_DAT0
  pinmux_metadata.pinmux_map[57] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      65,
  };  // SD0_DAT1
  pinmux_metadata.pinmux_map[58] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      66,
  };  // SD0_CLK
  pinmux_metadata.pinmux_map[59] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      67,
  };  // SD0_DAT2
  pinmux_metadata.pinmux_map[60] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      68,
  };  // SD0_DAT3
  pinmux_metadata.pinmux_map[61] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      69,
  };  // SD0_CMD
  pinmux_metadata.pinmux_map[62] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      70,
  };  // SD0_CDn
  pinmux_metadata.pinmux_map[63] = {
      synaptics::PinmuxEntry::kGpio,
      0,
      71,
  };  // SD0_WP
  pinmux_metadata.pinmux_map[64] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      18,
  };  // NAND_IO0
  pinmux_metadata.pinmux_map[65] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      19,
  };  // NAND_IO1
  pinmux_metadata.pinmux_map[66] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      20,
  };  // NAND_IO2
  pinmux_metadata.pinmux_map[67] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      21,
  };  // NAND_IO3
  pinmux_metadata.pinmux_map[68] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      22,
  };  // NAND_IO4
  pinmux_metadata.pinmux_map[69] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      23,
  };  // NAND_IO5
  pinmux_metadata.pinmux_map[70] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      24,
  };  // NAND_IO6
  pinmux_metadata.pinmux_map[71] = {
      synaptics::PinmuxEntry::kMuxOnly,
      0,
      25,
  };  // NAND_IO7

  static const std::vector<fpbus::Mmio> gpio_mmios{
      {{
          .base = as370::kPinmuxBase,
          .length = as370::kPinmuxSize,
      }},
      {{
          .base = as370::kGpio1Base,
          .length = as370::kGpioSize,
      }},
      {{
          .base = as370::kGpio2Base,
          .length = as370::kGpioSize,
      }},
  };

  static const std::vector<fpbus::Irq> gpio_irqs{
      {{
          .irq = as370::kGpio1Irq,
          .mode = ZX_INTERRUPT_MODE_LEVEL_HIGH,
      }},
  };

  const gpio_pin_t gpio_pins[] = {
      DECL_GPIO_PIN(GPIO_MIC_MUTE_STATUS), DECL_GPIO_PIN(GPIO_AMP_EN),
      DECL_GPIO_PIN(GPIO_LED_TOUCH_RESET), DECL_GPIO_PIN(GPIO_TOUCH_IRQ),
      DECL_GPIO_PIN(GPIO_WLAN_EN),
  };

  std::vector<fpbus::Metadata> gpio_metadata{
      {{
          .type = DEVICE_METADATA_GPIO_PINS,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&gpio_pins),
              reinterpret_cast<const uint8_t*>(&gpio_pins) + sizeof(gpio_pins)),
      }},
      {{
          .type = DEVICE_METADATA_PRIVATE,
          .data = std::vector<uint8_t>(
              reinterpret_cast<const uint8_t*>(&pinmux_metadata),
              reinterpret_cast<const uint8_t*>(&pinmux_metadata) + sizeof(pinmux_metadata)),
      }},
  };

  fpbus::Node gpio_dev;
  gpio_dev.name() = "gpio";
  gpio_dev.vid() = PDEV_VID_SYNAPTICS;
  gpio_dev.pid() = PDEV_PID_SYNAPTICS_AS370;
  gpio_dev.did() = PDEV_DID_SYNAPTICS_GPIO;
  gpio_dev.mmio() = gpio_mmios;
  gpio_dev.irq() = gpio_irqs;
  gpio_dev.metadata() = gpio_metadata;

  fidl::Arena<> fidl_arena;
  fdf::Arena arena('GPIO');
  auto result = pbus_.buffer(arena)->ProtocolNodeAdd(ZX_PROTOCOL_GPIO_IMPL,
                                                     fidl::ToWire(fidl_arena, gpio_dev));
  if (!result.ok()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Gpio(gpio_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: ProtocolNodeAdd Gpio(gpio_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  gpio_impl_ = ddk::GpioImplProtocolClient(parent());
  if (!gpio_impl_.is_valid()) {
    zxlogf(ERROR, "%s: device_get_protocol failed", __func__);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

}  // namespace board_pinecrest
