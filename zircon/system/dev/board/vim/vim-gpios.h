// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <soc/aml-s912/s912-gpio.h>

#define GPIO_WIFI_DEBUG     S912_GPIODV(13)
#define GPIO_THERMAL_FAN_O  S912_GPIODV(14)
#define GPIO_THERMAL_FAN_1  S912_GPIODV(15)
#define GPIO_ETH_MAC_RST    S912_GPIOZ(14)
#define GPIO_ETH_MAC_INTR   S912_GPIOZ(15)
