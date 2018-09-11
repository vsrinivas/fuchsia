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
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/platform-proxy.h>
#include <zircon/pixelformat.h>

#include "aml-canvas.h"

static void aml_canvas_release(void* ctx) {
    aml_canvas_t* canvas = ctx;
    mmio_buffer_release(&canvas->dmc_regs);
    for (uint32_t index = 0; index < NUM_CANVAS_ENTRIES; index++) {
        if (canvas->pmt_handle[index] != ZX_HANDLE_INVALID) {
            zx_pmt_unpin(canvas->pmt_handle[index]);
            canvas->pmt_handle[index] = ZX_HANDLE_INVALID;
        }
    }
    free(canvas);
}

static zx_status_t aml_canvas_config(void* ctx, zx_handle_t vmo,
                                     size_t offset, const canvas_info_t* info,
                                     uint8_t* canvas_idx) {
    aml_canvas_t* canvas = ctx;
    zx_status_t status = ZX_OK;

    if (!info || !canvas_idx) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t size = ROUNDUP((info->stride_bytes * info->height) +
                                (offset & (PAGE_SIZE - 1)),
                            PAGE_SIZE);
    uint32_t index;
    zx_paddr_t paddr;
    mtx_lock(&canvas->lock);

    uint32_t height = info->height;
    uint32_t width = info->stride_bytes;

    if (!IS_ALIGNED(height, 8) || !IS_ALIGNED(width, 8)) {
        CANVAS_ERROR("Height or width is not aligned\n");
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    // find an unused canvas index
    for (index = 0; index < NUM_CANVAS_ENTRIES; index++) {
        if (canvas->pmt_handle[index] == ZX_HANDLE_INVALID) {
            break;
        }
    }

    if (index == NUM_CANVAS_ENTRIES) {
        CANVAS_ERROR("All canvas indexes are currently in use\n");
        status = ZX_ERR_NOT_FOUND;
        goto fail;
    }

    status = zx_bti_pin(canvas->bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE | ZX_BTI_CONTIGUOUS,
                        vmo, offset & ~(PAGE_SIZE - 1), size,
                        &paddr, 1,
                        &canvas->pmt_handle[index]);
    if (status != ZX_OK) {
        CANVAS_ERROR("zx_bti_pin failed %d \n", status);
        goto fail;
    }

    if (!IS_ALIGNED(paddr, 8)) {
        CANVAS_ERROR("Physical address is not aligned\n");
        status = ZX_ERR_INVALID_ARGS;
        zx_handle_close(canvas->pmt_handle[index]);
        canvas->pmt_handle[index] = ZX_HANDLE_INVALID;
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    zx_paddr_t start_addr = paddr + (offset & (PAGE_SIZE - 1));

    // set framebuffer address in DMC, read/modify/write
    uint32_t value = ((start_addr >> 3) & DMC_CAV_ADDR_LMASK) |
                     (((width >> 3) & DMC_CAV_WIDTH_LMASK) << DMC_CAV_WIDTH_LBIT);
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL, value);

    value = (((width >> 3) >> DMC_CAV_WIDTH_LWID) << DMC_CAV_WIDTH_HBIT) |
            ((height & DMC_CAV_HEIGHT_MASK) << DMC_CAV_HEIGHT_BIT) |
            ((info->blkmode & DMC_CAV_BLKMODE_MASK) << DMC_CAV_BLKMODE_BIT) |
            ((info->wrap & DMC_CAV_XWRAP) ? DMC_CAV_XWRAP : 0) |
            ((info->wrap & DMC_CAV_YWRAP) ? DMC_CAV_YWRAP : 0) |
            ((info->endianness & DMC_CAV_ENDIANNESS_MASK) << DMC_CAV_ENDIANNESS_BIT);
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH, value);

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | index);

    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

    *canvas_idx = index;
fail:
    zx_handle_close(vmo);
    mtx_unlock(&canvas->lock);
    return status;
}

static zx_status_t aml_canvas_free(void* ctx, uint8_t canvas_idx) {
    aml_canvas_t* canvas = ctx;

    mtx_lock(&canvas->lock);

    zx_pmt_unpin(canvas->pmt_handle[canvas_idx]);
    canvas->pmt_handle[canvas_idx] = ZX_HANDLE_INVALID;

    mtx_unlock(&canvas->lock);
    return ZX_OK;
}

static void aml_canvas_init(aml_canvas_t* canvas) {
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL, 0);
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH, 0);

    for (int index = 0; index < NUM_CANVAS_ENTRIES; index++) {
        WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | index);
        READ32_DMC_REG(DMC_CAV_LUT_DATAH);
    }
}

static void aml_canvas_unbind(void* ctx) {
    aml_canvas_t* canvas = ctx;
    device_remove(canvas->zxdev);
}

static zx_protocol_device_t aml_canvas_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = aml_canvas_release,
    .unbind = aml_canvas_unbind,
};

static canvas_protocol_ops_t canvas_ops = {
    .config = aml_canvas_config,
    .free = aml_canvas_free,
};

static void aml_canvas_proxy_cb(platform_proxy_args_t* args, void* cookie) {
    if (args->req->proto_id != ZX_PROTOCOL_AMLOGIC_CANVAS) {
        args->resp->status = ZX_ERR_NOT_SUPPORTED;
        return;
    }
    if (args->req_size < sizeof(rpc_canvas_rsp_t)) {
        args->resp->status = ZX_ERR_BUFFER_TOO_SMALL;
        return;
    }

    rpc_canvas_req_t* req = (rpc_canvas_req_t*)args->req;
    rpc_canvas_rsp_t* resp = (rpc_canvas_rsp_t*)args->resp;
    args->resp_actual_size = sizeof(*resp);
    args->resp_actual_handles = 0;
    uint32_t handles_consumed = 0;

    switch (req->header.op) {
    case CANVAS_CONFIG: {
        if (args->req_handle_count < 1) {
            args->resp->status = ZX_ERR_BUFFER_TOO_SMALL;
            return;
        }
        resp->header.status = aml_canvas_config(cookie, args->req_handles[0], req->offset,
                                                &req->info, &resp->idx);
        handles_consumed = 1;
        break;
    }
    case CANVAS_FREE: {
        resp->header.status = aml_canvas_free(cookie, req->idx);
        break;
    }
    default:
        for (uint32_t i = 0; i < args->req_handle_count; i++) {
            zx_handle_close(args->req_handles[i]);
        }
        args->resp->status = ZX_ERR_NOT_SUPPORTED;
        return;
    }
    for (uint32_t i = handles_consumed; i < args->req_handle_count; i++) {
        zx_handle_close(args->req_handles[i]);
    }
    args->resp->status = ZX_OK;
}

static zx_status_t aml_canvas_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_canvas_t* canvas = calloc(1, sizeof(aml_canvas_t));
    if (!canvas) {
        return ZX_ERR_NO_MEMORY;
    }

    // Get device protocol
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &canvas->pdev);
    if (status != ZX_OK) {
        CANVAS_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        CANVAS_ERROR("ZX_PROTOCOL_PLATFORM_BUS not available %d \n", status);
        goto fail;
    }

    // Get BTI handle
    status = pdev_get_bti(&canvas->pdev, 0, &canvas->bti);
    if (status != ZX_OK) {
        CANVAS_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    // Map all MMIOs
    status = pdev_map_mmio_buffer2(&canvas->pdev, 0,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &canvas->dmc_regs);
    if (status != ZX_OK) {
        CANVAS_ERROR("Could not map DMC registers %d\n", status);
        goto fail;
    }

    // Do basic initialization
    aml_canvas_init(canvas);

    mtx_init(&canvas->lock, mtx_plain);

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "aml-canvas",
        .ctx = canvas,
        .ops = &aml_canvas_device_protocol,
        .proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS,
    };

    status = device_add(parent, &args, &canvas->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    canvas->canvas.ops = &canvas_ops;
    canvas->canvas.ctx = canvas;

    // Register the canvas protocol with the platform bus
    pbus_register_protocol(&pbus, ZX_PROTOCOL_AMLOGIC_CANVAS, &canvas->canvas, aml_canvas_proxy_cb,
                           canvas);
    return ZX_OK;
fail:
    aml_canvas_release(canvas);
    return status;
}

static zx_driver_ops_t aml_canvas_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_canvas_bind,
};

ZIRCON_DRIVER_BEGIN(aml_canvas, aml_canvas_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CANVAS),
ZIRCON_DRIVER_END(aml_canvas)
