// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/canvas.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/scpi.h>
#include <ddk/protocol/usb-mode-switch.h>

namespace platform_bus {

// maximum transfer size we can proxy.
static constexpr size_t PROXY_MAX_TRANSFER_SIZE = 4096;

// Header for RPC requests.
typedef struct {
    zx_txid_t txid;
    uint32_t protocol;
    uint32_t op;
} rpc_req_header_t;

// Header for RPC responses.
typedef struct {
    zx_txid_t txid;
    zx_status_t status;
} rpc_rsp_header_t;

// ZX_PROTOCOL_PLATFORM_DEV proxy support.
enum {
    PDEV_GET_MMIO,
    PDEV_GET_INTERRUPT,
    PDEV_GET_BTI,
    PDEV_GET_DEVICE_INFO,
    PDEV_GET_BOARD_INFO,
};

typedef struct {
    rpc_req_header_t header;
    uint32_t index;
    uint32_t flags;
} rpc_pdev_req_t;

typedef struct {
    rpc_rsp_header_t header;
    zx_paddr_t paddr;
    size_t length;
    uint32_t irq;
    uint32_t mode;
    pdev_device_info_t device_info;
    pdev_board_info_t board_info;
} rpc_pdev_rsp_t;

// Maximum I2C transfer is I2C_MAX_TRANSFER_SIZE minus size of largest header.
static constexpr size_t I2C_MAX_TRANSFER_SIZE = (PROXY_MAX_TRANSFER_SIZE -
            (sizeof(rpc_pdev_req_t) > sizeof(rpc_pdev_rsp_t) ?
             sizeof(rpc_pdev_req_t) : sizeof(rpc_pdev_rsp_t)));

// ZX_PROTOCOL_USB_MODE_SWITCH  proxy support.
enum {
    UMS_SET_MODE,
};
typedef struct {
    rpc_req_header_t header;
    usb_mode_t usb_mode;
} rpc_ums_req_t;

// ZX_PROTOCOL_GPIO proxy support.
enum {
    GPIO_CONFIG,
    GPIO_SET_ALT_FUNCTION,
    GPIO_READ,
    GPIO_WRITE,
    GPIO_GET_INTERRUPT,
    GPIO_RELEASE_INTERRUPT,
    GPIO_SET_POLARITY,
};

typedef struct {
    rpc_req_header_t header;
    uint32_t index;
    uint32_t flags;
    uint32_t polarity;
    uint64_t alt_function;
    uint8_t value;
} rpc_gpio_req_t;

typedef struct {
    rpc_rsp_header_t header;
    uint8_t value;
} rpc_gpio_rsp_t;

// ZX_PROTOCOL_I2C proxy support.
enum {
    I2C_GET_MAX_TRANSFER,
    I2C_TRANSACT,
};

typedef struct {
    rpc_req_header_t header;
    uint32_t index;
    size_t write_length;
    size_t read_length;
    i2c_complete_cb complete_cb;
    void* cookie;
} rpc_i2c_req_t;

typedef struct {
    rpc_rsp_header_t header;
    size_t max_transfer;
    i2c_complete_cb complete_cb;
    void* cookie;
} rpc_i2c_rsp_t;

// ZX_PROTOCOL_CLK proxy support.
enum {
    CLK_ENABLE,
    CLK_DISABLE,
};

typedef struct {
    rpc_req_header_t header;
    uint32_t index;
} rpc_clk_req_t;

// ZX_PROTOCOL_SCPI proxy support.
enum {
    SCPI_GET_SENSOR,
    SCPI_GET_SENSOR_VALUE,
    SCPI_GET_DVFS_INFO,
    SCPI_GET_DVFS_IDX,
    SCPI_SET_DVFS_IDX,
};

typedef struct {
    rpc_req_header_t header;
    uint8_t power_domain;
    uint16_t idx;
    uint32_t sensor_id;
    char name[20];
} rpc_scpi_req_t;

typedef struct {
    rpc_rsp_header_t header;
    uint32_t sensor_value;
    uint16_t dvfs_idx;
    uint32_t sensor_id;
    scpi_opp_t opps;
} rpc_scpi_rsp_t;

// ZX_PROTOCOL_CANVAS proxy support.
enum {
    CANVAS_CONFIG,
    CANVAS_FREE,
};

typedef struct {
    rpc_req_header_t header;
    canvas_info_t info;
    size_t offset;
    uint8_t idx;
} rpc_canvas_req_t;

typedef struct {
    rpc_rsp_header_t header;
    uint8_t idx;
} rpc_canvas_rsp_t;

} // namespace platform_bus
