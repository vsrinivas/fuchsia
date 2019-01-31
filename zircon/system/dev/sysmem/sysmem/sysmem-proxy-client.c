// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/sysmem.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform/proxy.h>

#include "sysmem-proxy.h"

static zx_status_t sysmem_proxy_connect(void* ctx, zx_handle_t allocator2_request) {
    sysmem_proxy_t* proxy = ctx;
    rpc_sysmem_req_t req = {
        .header = {
            .proto_id = ZX_PROTOCOL_SYSMEM,
            .op = SYSMEM_CONNECT,
        },
    };
    rpc_sysmem_rsp_t resp;

    size_t size_actual, handle_count_actual;
    zx_status_t status = platform_proxy_proxy(
        &proxy->proxy, &req, sizeof(req), &allocator2_request, 1,
        &resp, sizeof(resp), &size_actual, NULL, 0, &handle_count_actual);
    return status;
}

static sysmem_protocol_ops_t sysmem_proxy_ops = {
    .connect = sysmem_proxy_connect,
};

static void sysmem_proxy_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t proxy_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = sysmem_proxy_release,
};

static zx_status_t sysmem_proxy_bind(void* ctx, zx_device_t* parent) {
    platform_proxy_protocol_t proxy;

    zx_status_t status = device_get_protocol(
        parent, ZX_PROTOCOL_PLATFORM_PROXY, &proxy);
    if (status != ZX_OK) {
        return status;
    }

    sysmem_proxy_t* sysmem_proxy = calloc(1, sizeof(sysmem_proxy_t));
    if (!sysmem_proxy) {
        return ZX_ERR_NO_MEMORY;
    }

    memcpy(&sysmem_proxy->proxy, &proxy, sizeof(proxy));
    sysmem_proxy->sysmem.ctx = sysmem_proxy;
    sysmem_proxy->sysmem.ops = &sysmem_proxy_ops;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "sysmem-proxy",
        .ctx = sysmem_proxy,
        .ops = &proxy_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &sysmem_proxy->zxdev);
    if (status != ZX_OK) {
        free(sysmem_proxy);
        return status;
    }

    status = platform_proxy_register_protocol(
        &proxy, ZX_PROTOCOL_SYSMEM,
        &sysmem_proxy->sysmem, sizeof(sysmem_proxy->sysmem));
    if (status != ZX_OK) {
        device_remove(sysmem_proxy->zxdev);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t sysmem_proxy_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = sysmem_proxy_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(sysmem_proxy, sysmem_proxy_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_PROXY),
    BI_MATCH_IF(EQ, BIND_PLATFORM_PROTO, ZX_PROTOCOL_SYSMEM),
ZIRCON_DRIVER_END(sysmem_proxy)
// clang-format on
