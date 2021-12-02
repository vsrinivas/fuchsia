// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_VIM3_VIM3_GPIOS_H_
#define SRC_DEVICES_BOARD_DRIVERS_VIM3_VIM3_GPIOS_H_

#include <soc/aml-a311d/a311d-gpio.h>

// VIM3 specific assignments should be placed in this file.
// SoC specific definitions should be placed in soc/aml-a311d-gpio.h

// Ethernet
#define VIM3_ETH_MAC_INTR A311D_GPIOZ(14)

// 40-pin J4 header
#define VIM3_J4_PIN_39 A311D_GPIOZ(15)

// SDIO
#define A311D_SDIO_D0 A311D_GPIOX(0)
#define A311D_SDIO_D1 A311D_GPIOX(1)
#define A311D_SDIO_D2 A311D_GPIOX(2)
#define A311D_SDIO_D3 A311D_GPIOX(3)
#define A311D_SDIO_CLK A311D_GPIOX(4)
#define A311D_SDIO_CMD A311D_GPIOX(5)

// Display
#define VIM3_HPD_IN A311D_GPIOH(2)

// USB Power Delivery (Fusb302)
#define VIM3_FUSB302_INT A311D_GPIOAO(8)

// Make the GPIO expander indices start after the SoC GPIOs to avoid any overlap.
#define VIM3_EXPANDER_GPIO_START (A311D_GPIOE_START + A311D_GPIOE_COUNT)
#define VIM3_EXPANDER_GPIO(n) (VIM3_EXPANDER_GPIO_START + (n))

#define VIM3_SD_MODE VIM3_EXPANDER_GPIO(7)

#endif  // SRC_DEVICES_BOARD_DRIVERS_VIM3_VIM3_GPIOS_H_
