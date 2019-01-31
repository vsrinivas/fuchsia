// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/platform/proxy.h>
#include <ddk/protocol/sysmem.h>

// Proxy request IDs.
enum {
    SYSMEM_CONNECT,
};

// Proxy request.
typedef struct {
    platform_proxy_req_t header;

    // We don't need any sysmem-specific fields, since the handle is delivered
    // separately.
} rpc_sysmem_req_t;

// Proxy response.
typedef struct {
    platform_proxy_rsp_t header;

    // We don't need any sysmem-specific fields.
} rpc_sysmem_rsp_t;

// Context for driver proxy.
typedef struct {
    zx_device_t* zxdev;
    platform_proxy_protocol_t proxy;
    sysmem_protocol_t sysmem;
} sysmem_proxy_t;
