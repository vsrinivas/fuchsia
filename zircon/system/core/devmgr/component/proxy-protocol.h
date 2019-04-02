// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace component {

// Maximum transfer size we can proxy.
static constexpr size_t kProxyMaxTransferSize = 4096;

/// Header for RPC requests.
struct ProxyRequest {
    uint32_t txid;
    uint32_t proto_id;
};

/// Header for RPC responses.
struct ProxyResponse {
    uint32_t txid;
    zx_status_t status;
};

// ZX_PROTOCOL_GPIO proxy support.
enum class GpioOp {
    CONFIG_IN,
    CONFIG_OUT,
    SET_ALT_FUNCTION,
    READ,
    WRITE,
    GET_INTERRUPT,
    RELEASE_INTERRUPT,
    SET_POLARITY,
};

struct GpioProxyRequest {
    ProxyRequest header;
    GpioOp op;
    uint32_t flags;
    uint32_t polarity;
    uint64_t alt_function;
    uint8_t value;
};

struct GpioProxyResponse {
    ProxyResponse header;
    uint8_t value;
};

// ZX_PROTOCOL_CLOCK proxy support.
enum class ClockOp {
    ENABLE,
    DISABLE,
};

struct ClockProxyRequest {
    ProxyRequest header;
    ClockOp op;
    uint32_t index;
};

// ZX_PROTOCOL_POWER proxy support.
enum class PowerOp {
    ENABLE,
    DISABLE,
    GET_STATUS,
};

struct PowerProxyRequest {
    ProxyRequest header;
    PowerOp op;
};

struct PowerProxyResponse {
    ProxyResponse header;
    power_domain_status_t status;
};

// ZX_PROTOCOL_SYSMEM proxy support.
enum class SysmemOp {
    CONNECT,
};

struct SysmemProxyRequest {
    ProxyRequest header;
    SysmemOp op;
};

// ZX_PROTOCOL_AMLOGIC_CANVAS proxy support.
enum class AmlogicCanvasOp {
    CONFIG,
    FREE,
};

struct AmlogicCanvasProxyRequest {
    ProxyRequest header;
    AmlogicCanvasOp op;
    size_t offset;
    canvas_info_t info;
    uint8_t canvas_idx;
};

struct AmlogicCanvasProxyResponse {
    ProxyResponse header;
    uint8_t canvas_idx;
};

} // namespace component
