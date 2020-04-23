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

#endif  // SRC_DEVICES_BOARD_DRIVERS_VIM3_VIM3_GPIOS_H_
