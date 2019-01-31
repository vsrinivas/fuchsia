// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <zircon/listnode.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>

#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>

typedef enum {
    GPIO_MUX,
    GPIO_PD,
    GPIO_INT,
    GPIO_COUNT,
} hdmi_gpio_if_t;

typedef struct {
    zx_device_t* zdev;
    i2c_protocol_t i2c_main;
    i2c_protocol_t i2c_cec;
    i2c_protocol_t i2c_edid;
} adv7533_i2c_t;

typedef struct {
    zx_device_t* zxdev;
    gpio_protocol_t gpios[GPIO_COUNT];
} hdmi_gpio_t;

typedef struct {
    zx_device_t*             zxdev;
    pdev_protocol_t          pdev;
    zx_device_t*             parent;
    mmio_buffer_t            mmio;

    adv7533_i2c_t            i2c_dev;        // ADV7533 I2C device
    hdmi_gpio_t              hdmi_gpio;      // ADV7533-related GPIOs

    char                     write_buf[64];  // scratch buffer used for the i2c driver
} display_t;
