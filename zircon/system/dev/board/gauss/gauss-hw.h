// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <soc/aml-a113/a113-gpio.h>

// pins for I2C A and B
#define I2C_SCK_A A113_GPIOZ(6)
#define I2C_SDA_A A113_GPIOZ(7)
#define I2C_SCK_B A113_GPIOZ(8)
#define I2C_SDA_B A113_GPIOZ(9)

// pins for TDM C
#define TDM_BCLK_C A113_GPIOA(2)
#define TDM_FSYNC_C A113_GPIOA(3)
#define TDM_MOSI_C A113_GPIOA(4)
#define TDM_MISO_C A113_GPIOA(5)

#define SPK_MUTEn A113_GPIOA(20)

// GPIO for USB VBus
#define USB_VBUS_GPIO A113_GPIOAO(5)
