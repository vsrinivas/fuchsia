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

#endif  // SRC_DEVICES_BOARD_DRIVERS_VIM3_VIM3_GPIOS_H_
