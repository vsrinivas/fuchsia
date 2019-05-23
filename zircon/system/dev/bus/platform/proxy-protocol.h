// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/platform/device.h>

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
    PDEV_GET_METADATA,
};

/// Header for RPC requests.
struct platform_proxy_req_t {
    uint32_t txid;
    uint32_t proto_id;
    uint32_t op;
};

/// Header for RPC responses.
struct platform_proxy_rsp_t {
    uint32_t txid;
    zx_status_t status;
};

struct rpc_pdev_req_t {
    platform_proxy_req_t header;
    uint32_t index;
    uint32_t flags;
};

struct rpc_pdev_rsp_t {
    platform_proxy_rsp_t header;
    zx_paddr_t paddr;
    size_t length;
    uint32_t irq;
    uint32_t mode;
    pdev_device_info_t device_info;
    pdev_board_info_t board_info;
    uint32_t metadata_type;
    uint32_t metadata_length;
};

// Maximum metadata size that can be returned via PDEV_DEVICE_GET_METADATA.
static constexpr uint32_t PROXY_MAX_METADATA_SIZE =
    (PROXY_MAX_TRANSFER_SIZE - sizeof(rpc_pdev_rsp_t));

struct rpc_pdev_metadata_rsp_t {
    rpc_pdev_rsp_t pdev;
    uint8_t metadata[PROXY_MAX_METADATA_SIZE];
};

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

struct rpc_gpio_req_t {
    platform_proxy_req_t header;
    uint32_t index;
    uint32_t flags;
    uint32_t polarity;
    uint64_t alt_function;
    uint8_t value;
};

struct rpc_gpio_rsp_t {
    platform_proxy_rsp_t header;
    uint8_t value;
};

// ZX_PROTOCOL_CLOCK proxy support.
enum {
    CLK_ENABLE,
    CLK_DISABLE,
};

struct rpc_clk_req_t {
    platform_proxy_req_t header;
    uint32_t index;
};

// ZX_PROTOCOL_SYSMEM proxy support.
enum {
    SYSMEM_CONNECT,
    SYSMEM_REGISTER_HEAP,
};

struct rpc_sysmem_req_t {
    platform_proxy_req_t header;
    uint64_t heap;
};

// ZX_PROTOCOL_AMLOGIC_CANVAS proxy support.
enum {
    AMLOGIC_CANVAS_CONFIG,
    AMLOGIC_CANVAS_FREE,
};

struct rpc_amlogic_canvas_req_t {
    platform_proxy_req_t header;
    size_t offset;
    canvas_info_t info;
    uint8_t canvas_idx;
};

struct rpc_amlogic_canvas_rsp_t {
    platform_proxy_rsp_t header;
    uint8_t canvas_idx;
};

} // namespace platform_bus
