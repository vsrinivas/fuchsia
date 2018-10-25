// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-proxy.h>

namespace platform_bus {

// Maximum transfer size we can proxy.
static constexpr size_t PROXY_MAX_TRANSFER_SIZE = 4096;

// Device ID for a top level platform device (that is, an immediate child of the platform bus).
static constexpr uint32_t ROOT_DEVICE_ID = 0;

// ZX_PROTOCOL_PDEV proxy support.
enum {
    PDEV_GET_MMIO,
    PDEV_GET_INTERRUPT,
    PDEV_GET_BTI,
    PDEV_GET_SMC,
    PDEV_GET_DEVICE_INFO,
    PDEV_GET_BOARD_INFO,
    PDEV_DEVICE_ADD,
    PDEV_GET_METADATA,
    PDEV_GET_PROTOCOLS,
};

typedef struct {
    platform_proxy_req_t header;
    uint32_t index;
    uint32_t flags;
} rpc_pdev_req_t;

typedef struct {
    platform_proxy_rsp_t header;
    zx_paddr_t paddr;
    size_t length;
    uint32_t irq;
    uint32_t mode;
    pdev_device_info_t device_info;
    pdev_board_info_t board_info;
    uint32_t device_id;
    uint32_t metadata_type;
    uint32_t metadata_length;
    uint32_t protocol_count;
} rpc_pdev_rsp_t;

// Maximum metadata size that can be returned via PDEV_DEVICE_GET_METADATA.
static constexpr uint32_t PROXY_MAX_METADATA_SIZE =
    (PROXY_MAX_TRANSFER_SIZE - sizeof(rpc_pdev_rsp_t));

typedef struct {
    rpc_pdev_rsp_t pdev;
    uint8_t metadata[PROXY_MAX_METADATA_SIZE];
} rpc_pdev_metadata_rsp_t;

// Maximum number of protocols that can be returned via PDEV_GET_PROTOCOLS.
static constexpr size_t PROXY_MAX_PROTOCOLS = ((PLATFORM_PROXY_MAX_DATA - sizeof(rpc_pdev_rsp_t))
                                                / sizeof(uint32_t));

// Maximum I2C transfer is I2C_MAX_TRANSFER_SIZE minus size of largest header.
static constexpr uint32_t I2C_MAX_TRANSFER_SIZE = (PROXY_MAX_TRANSFER_SIZE -
            (sizeof(rpc_pdev_req_t) > sizeof(rpc_pdev_rsp_t) ?
             sizeof(rpc_pdev_req_t) : sizeof(rpc_pdev_rsp_t)));

// ZX_PROTOCOL_GPIO proxy support.
enum {
    GPIO_CONFIG_IN,
    GPIO_CONFIG_OUT,
    GPIO_SET_ALT_FUNCTION,
    GPIO_READ,
    GPIO_WRITE,
    GPIO_GET_INTERRUPT,
    GPIO_RELEASE_INTERRUPT,
    GPIO_SET_POLARITY,
};

typedef struct {
    platform_proxy_req_t header;
    uint32_t index;
    uint32_t flags;
    uint32_t polarity;
    uint64_t alt_function;
    uint8_t value;
} rpc_gpio_req_t;

typedef struct {
    platform_proxy_rsp_t header;
    uint8_t value;
} rpc_gpio_rsp_t;

// ZX_PROTOCOL_I2C proxy support.
enum {
    I2C_GET_MAX_TRANSFER,
    I2C_TRANSACT,
};

typedef struct {
    platform_proxy_req_t header;
    uint32_t index;
    i2c_transact_callback transact_cb;
    void* cookie;
    size_t cnt;
} rpc_i2c_req_t;

typedef struct {
    platform_proxy_rsp_t header;
    size_t max_transfer;
    i2c_transact_callback transact_cb;
    void* cookie;
} rpc_i2c_rsp_t;

// ZX_PROTOCOL_CLK proxy support.
enum {
    CLK_ENABLE,
    CLK_DISABLE,
};

typedef struct {
    platform_proxy_req_t header;
    uint32_t index;
} rpc_clk_req_t;

} // namespace platform_bus
