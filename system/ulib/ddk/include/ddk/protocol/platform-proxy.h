// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Maximum transfer size we can proxy.
#define PLATFORM_PROXY_MAX_DATA 4096

// Header for RPC requests.
typedef struct {
    zx_txid_t txid;
    uint32_t device_id;
    uint32_t proto_id;
    uint32_t op;
} platform_proxy_req_t;

// Header for RPC responses.
typedef struct {
    zx_txid_t txid;
    zx_status_t status;
} platform_proxy_rsp_t;

typedef struct {
    // Pointer to request buffer.
    platform_proxy_req_t* req;
    // Size of req.
    uint32_t req_size;
    // Pointer to response buffer.
    platform_proxy_rsp_t* resp;
    // Size of resp.
    uint32_t resp_size;
    // Handles passed with request (callee takes ownership).
    zx_handle_t* req_handles;
    // Number of handles passed with request.
    uint32_t req_handle_count;
    // Buffer for receiving handles with response.
    zx_handle_t* resp_handles;
    // Number of resp_handles we expect to receive.
    uint32_t resp_handle_count;
    // Number of bytes received in resp_buf.
    size_t resp_actual_size;
    // Number of handles received in resp_handles
     size_t resp_actual_handles;
} platform_proxy_args_t;

typedef struct {
    zx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, const void* protocol);
    zx_status_t (*proxy)(void* ctx, platform_proxy_args_t* args);
} platform_proxy_protocol_ops_t;

typedef struct {
    platform_proxy_protocol_ops_t* ops;
    void* ctx;
} platform_proxy_protocol_t;

// Used by protocol client drivers to register their local protocol implementation
// with the platform proxy driver.
static inline zx_status_t platform_proxy_register_protocol(platform_proxy_protocol_t* proxy,
                                                           uint32_t proto_id,
                                                           const void* protocol) {
    return proxy->ops->register_protocol(proxy->ctx, proto_id, protocol);
}

// Used by protocol client drivers to proxy a protocol call to the protocol implementation driver
// in the platform bus driver's devhost.
static inline zx_status_t platform_proxy_proxy(platform_proxy_protocol_t* proxy,
                                               platform_proxy_args_t* args) {
    return proxy->ops->proxy(proxy->ctx, args);
}
__END_CDECLS;
