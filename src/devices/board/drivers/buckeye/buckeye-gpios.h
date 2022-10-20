// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_BUCKEYE_BUCKEYE_GPIOS_H_
#define SRC_DEVICES_BOARD_DRIVERS_BUCKEYE_BUCKEYE_GPIOS_H_

#include <soc/aml-a5/a5-gpio.h>

namespace buckeye {
namespace fpbus = fuchsia_hardware_platform_bus;

// clang-format off

// GPIO B Bank
#define GPIO_EMMC_RST_L         A5_GPIOB(9)

// GPIO C Bank
#define GPIO_SOC_AMP_EN_H       A5_GPIOC(9)

// GPIO E Bank
// GPIO H Bank
// GPIO D Bank
#define GPIO_VOL_UP_L           A5_GPIOD(2)
#define GPIO_MIC_MUTE           A5_GPIOD(3)
#define GPIO_SNS_SPI1_PD0N      A5_GPIOD(4)
#define GPIO_VOL_DN_L           A5_GPIOD(15)

// GPIO T Bank
#define GPIO_UNKNOWN_SPI_SS0    A5_GPIOT(4)
#define GPIO_RF_SPI_RESETN      A5_GPIOT(7)
#define GPIO_PANEL_SPI_SS0      A5_GPIOT(10)

// GPIO X Bank
#define GPIO_WL_PDN             A5_GPIOX(6)
#define GPIO_SOC_WIFI_32k768    A5_GPIOX(16)
#define GPIO_SOC_BT_REG_ON      A5_GPIOX(17)

// GPIO Z Bank
#define GPIO_AMP_EN             A5_GPIOZ(10)

// clang-format on

}  // namespace buckeye

#endif  // SRC_DEVICES_BOARD_DRIVERS_BUCKEYE_BUCKEYE_GPIOS_H_
