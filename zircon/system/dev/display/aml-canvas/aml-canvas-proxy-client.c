// Copyright 2018 The Fuchsia Authors. All rights reserved.
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
#include <ddk/protocol/amlogiccanvas.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform/proxy.h>
#include <zircon/pixelformat.h>

#include "aml-canvas.h"

static zx_status_t aml_canvas_proxy_config(void* ctx, zx_handle_t vmo,
                                           size_t offset, const canvas_info_t* info,
                                           uint8_t* canvas_idx) {
    aml_canvas_proxy_t* proxy = ctx;
    rpc_canvas_req_t req = {
        .header = {
            .proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS,
            .op = CANVAS_CONFIG,
        },
        .offset = offset,
    };
    rpc_canvas_rsp_t resp;

    memcpy((void*)&req.info, info, sizeof(canvas_info_t));

    size_t size_actual, handle_count_actual;
    zx_status_t status = platform_proxy_proxy(&proxy->proxy, &req, sizeof(req), &vmo, 1, &resp,
                                              sizeof(resp), &size_actual, NULL, 0,
                                              &handle_count_actual);
    if (status == ZX_OK && size_actual == sizeof(resp)) {
        *canvas_idx = resp.idx;
    }
    return status;
}

static zx_status_t aml_canvas_proxy_free(void* ctx, uint8_t canvas_idx) {
    aml_canvas_proxy_t* proxy = ctx;
    rpc_canvas_req_t req = {
        .header = {
            .proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS,
            .op = CANVAS_FREE,
        },
        .idx = canvas_idx,
    };
    rpc_canvas_rsp_t resp;

    size_t size_actual, handle_count_actual;
    return platform_proxy_proxy(&proxy->proxy, &req, sizeof(req), NULL, 0, &resp, sizeof(resp),
                                &size_actual, NULL, 0, &handle_count_actual);
}

static amlogic_canvas_protocol_ops_t canvas_proxy_ops = {
    .config = aml_canvas_proxy_config,
    .free   = aml_canvas_proxy_free,
};

static void aml_canvas_proxy_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t proxy_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_canvas_proxy_release,
};

static zx_status_t aml_canvas_proxy_bind(void* ctx, zx_device_t* parent) {
    platform_proxy_protocol_t proxy;

    zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_PROXY, &proxy);
    if (status != ZX_OK) {
        return status;
    }

    aml_canvas_proxy_t* canvas = calloc(1, sizeof(aml_canvas_proxy_t));
    if (!canvas) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(&canvas->proxy, &proxy, sizeof(proxy));
    canvas->canvas.ctx = canvas;
    canvas->canvas.ops = &canvas_proxy_ops;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-canvas-proxy",
        .ctx = canvas,
        .ops = &proxy_device_protocol,
        .flags = DEVICE_ADD_NON_BINDABLE,
    };

    status = device_add(parent, &args, &canvas->zxdev);
    if (status != ZX_OK) {
        free(canvas);
        return status;
    }

    status = platform_proxy_register_protocol(&proxy, ZX_PROTOCOL_AMLOGIC_CANVAS,
                                              &canvas->canvas, sizeof(canvas->canvas));
    if (status != ZX_OK) {
        device_remove(canvas->zxdev);
        return status;
    }

    return ZX_OK;
}

static zx_driver_ops_t aml_canvas_proxy_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = aml_canvas_proxy_bind,
};

ZIRCON_DRIVER_BEGIN(aml_canvas_proxy, aml_canvas_proxy_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_PROXY),
    BI_MATCH_IF(EQ, BIND_PLATFORM_PROTO, ZX_PROTOCOL_AMLOGIC_CANVAS),
ZIRCON_DRIVER_END(aml_canvas_proxy)
