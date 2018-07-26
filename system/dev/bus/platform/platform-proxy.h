// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/mailbox.h>
#include <ddk/protocol/scpi.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <ddk/protocol/canvas.h>

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

    // ZX_PROTOCOL_MAILBOX
    PDEV_MAILBOX_SEND_CMD,

    // ZX_PROTOCOL_SCPI
    PDEV_SCPI_GET_SENSOR,
    PDEV_SCPI_GET_SENSOR_VALUE,
    PDEV_SCPI_GET_DVFS_INFO,
    PDEV_SCPI_GET_DVFS_IDX,
    PDEV_SCPI_SET_DVFS_IDX,

    // ZX_PROTOCOL_CANVAS
    PDEV_CANVAS_CONFIG,
    PDEV_CANCAS_FREE,
};

// context for canvas
typedef struct {
    canvas_info_t info;
    size_t offset;
} pdev_canvas_ctx_t;

// context for mailbox
typedef struct {
    mailbox_channel_t channel;
    mailbox_data_buf_t mdata;
} pdev_mailbox_ctx_t;

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
        uint64_t gpio_alt_function;
        uint8_t gpio_value;
        uint8_t canvas_idx;
        pdev_i2c_txn_ctx_t i2c_txn;
        uint32_t i2c_bitrate;
        uint32_t flags;
        pdev_mailbox_ctx_t mailbox;
        pdev_canvas_ctx_t canvas;
        uint8_t scpi_power_domain;
        uint32_t scpi_sensor_id;
        char scpi_name[20];
    };
} pdev_req_t;

typedef struct {
    zx_txid_t txid;
    zx_status_t status;
    union {
        usb_mode_t usb_mode;
        uint8_t gpio_value;
        uint8_t canvas_idx;
        pdev_i2c_txn_ctx_t i2c_txn;
        size_t i2c_max_transfer;
        struct {
            zx_off_t offset;
            size_t length;
            zx_paddr_t paddr;
        } mmio;
        pdev_device_info_t info;
        pdev_mailbox_ctx_t mailbox;
        uint32_t scpi_sensor_value;
        uint16_t scpi_dvfs_idx;
        uint32_t scpi_sensor_id;
        scpi_opp_t scpi_opps;
    };
} pdev_resp_t;
