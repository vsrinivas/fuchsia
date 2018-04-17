// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/usb-mode-switch.h>

// maximum transfer size we can proxy.
#define PDEV_I2C_MAX_TRANSFER_SIZE 4096

// RPC ops
enum {
    // ZX_PROTOCOL_PLATFORM_DEV
    PDEV_GET_MMIO = 1,
    PDEV_GET_INTERRUPT,
    PDEV_GET_BTI,
    PDEV_GET_DEVICE_INFO,

    // ZX_PROTOCOL_USB_MODE_SWITCH
    PDEV_UMS_GET_INITIAL_MODE,
    PDEV_UMS_SET_MODE,

    // ZX_PROTOCOL_GPIO
    PDEV_GPIO_CONFIG,
    PDEV_GPIO_SET_ALT_FUNCTION,
    PDEV_GPIO_READ,
    PDEV_GPIO_WRITE,
    PDEV_GPIO_GET_INTERRUPT,
    PDEV_GPIO_RELEASE_INTERRUPT,
    PDEV_GPIO_SET_POLARITY,

    // ZX_PROTOCOL_I2C
    PDEV_I2C_GET_MAX_TRANSFER,
    PDEV_I2C_TRANSACT,

    // ZX_PROTOCOL_CLK
    PDEV_CLK_ENABLE,
    PDEV_CLK_DISABLE,
};

// context for i2c_transact
typedef struct {
    size_t write_length;
    size_t read_length;
    i2c_complete_cb complete_cb;
    void* cookie;
} pdev_i2c_txn_ctx_t;

typedef struct {
    size_t size;
    uint32_t align_log2;
    uint32_t cache_policy;
} pdev_config_vmo_t;

typedef struct pdev_req {
    zx_txid_t txid;
    uint32_t op;
    uint32_t index;
    union {
        usb_mode_t usb_mode;
        uint32_t gpio_flags;
        uint32_t gpio_alt_function;
        uint8_t gpio_value;
        pdev_i2c_txn_ctx_t i2c_txn;
        uint32_t i2c_bitrate;
        uint32_t flags;
    };
} pdev_req_t;

typedef struct {
    zx_txid_t txid;
    zx_status_t status;
    union {
        usb_mode_t usb_mode;
        uint8_t gpio_value;
        pdev_i2c_txn_ctx_t i2c_txn;
        size_t i2c_max_transfer;
        struct {
            zx_off_t offset;
            size_t length;
        } mmio;
        pdev_device_info_t info;
    };
} pdev_resp_t;
