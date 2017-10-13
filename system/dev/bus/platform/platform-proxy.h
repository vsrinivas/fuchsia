// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/usb-mode-switch.h>

// RPC ops
enum {
    // ZX_PROTOCOL_PLATFORM_DEV
    PDEV_GET_MMIO = 1,
    PDEV_GET_INTERRUPT,

    // ZX_PROTOCOL_USB_MODE_SWITCH
    PDEV_UMS_GET_INITIAL_MODE,
    PDEV_UMS_SET_MODE,

    // ZX_PROTOCOL_GPIO
    PDEV_GPIO_CONFIG,
    PDEV_GPIO_READ,
    PDEV_GPIO_WRITE,
};

typedef struct {
    zx_txid_t txid;
    uint32_t op;
    uint32_t index;
    union {
        usb_mode_t usb_mode;
        gpio_config_flags_t gpio_flags;
        uint8_t gpio_value;
    };
} pdev_req_t;

typedef struct {
    zx_txid_t txid;
    zx_status_t status;
    union {
        usb_mode_t usb_mode;
        uint8_t gpio_value;
    };
} pdev_resp_t;
