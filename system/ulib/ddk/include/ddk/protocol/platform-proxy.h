// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_proxy.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef struct platform_proxy_rsp platform_proxy_rsp_t;
typedef struct platform_proxy_req platform_proxy_req_t;
typedef struct platform_proxy_protocol platform_proxy_protocol_t;

// Declarations

#define PLATFORM_PROXY_MAX_DATA UINT32_C(4096)

// Header for RPC responses.
struct platform_proxy_rsp {
    uint32_t txid;
    zx_status_t status;
};

// Header for RPC requests.
struct platform_proxy_req {
    uint32_t txid;
    uint32_t device_id;
    uint32_t proto_id;
    uint32_t op;
};

typedef struct platform_proxy_protocol_ops {
    zx_status_t (*register_protocol)(void* ctx, uint32_t proto_id, const void* protocol_buffer,
                                     size_t protocol_size);
    zx_status_t (*proxy)(void* ctx, const void* req_buffer, size_t req_size,
                         const zx_handle_t* req_handle_list, size_t req_handle_count,
                         void* out_resp_buffer, size_t resp_size, size_t* out_resp_actual,
                         zx_handle_t* out_resp_handle_list, size_t resp_handle_count,
                         size_t* out_resp_handle_actual);
} platform_proxy_protocol_ops_t;

struct platform_proxy_protocol {
    platform_proxy_protocol_ops_t* ops;
    void* ctx;
};

// Used by protocol client drivers to register their local protocol implementation
// with the platform proxy driver.
static inline zx_status_t platform_proxy_register_protocol(const platform_proxy_protocol_t* proto,
                                                           uint32_t proto_id,
                                                           const void* protocol_buffer,
                                                           size_t protocol_size) {
    return proto->ops->register_protocol(proto->ctx, proto_id, protocol_buffer, protocol_size);
}
// Used by protocol client drivers to proxy a protocol call to the protocol implementation
// driver in the platform bus driver's devhost.
static inline zx_status_t
platform_proxy_proxy(const platform_proxy_protocol_t* proto, const void* req_buffer,
                     size_t req_size, const zx_handle_t* req_handle_list, size_t req_handle_count,
                     void* out_resp_buffer, size_t resp_size, size_t* out_resp_actual,
                     zx_handle_t* out_resp_handle_list, size_t resp_handle_count,
                     size_t* out_resp_handle_actual) {
    return proto->ops->proxy(proto->ctx, req_buffer, req_size, req_handle_list, req_handle_count,
                             out_resp_buffer, resp_size, out_resp_actual, out_resp_handle_list,
                             resp_handle_count, out_resp_handle_actual);
}

__END_CDECLS;
