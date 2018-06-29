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
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/canvas.h>
#include <zircon/pixelformat.h>
#include "aml-canvas.h"

static void aml_canvas_release(void* ctx) {
    aml_canvas_t* canvas = ctx;
    io_buffer_release(&canvas->dmc_regs);
    for (uint32_t index = 0; index < NUM_CANVAS_ENTRIES; index++) {
        if (canvas->pmt_handle[index] != ZX_HANDLE_INVALID) {
            zx_pmt_unpin(canvas->pmt_handle[index]);
            canvas->pmt_handle[index] = ZX_HANDLE_INVALID;
        }
    }
    free(canvas);
}

static zx_status_t aml_canvas_config(void* ctx, zx_handle_t vmo,
                                     size_t offset, canvas_info_t* info,
                                     uint8_t* canvas_idx) {
    aml_canvas_t* canvas = ctx;
    zx_status_t status = ZX_OK;

    if (!info || !canvas_idx) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t size = ROUNDUP((info->stride_bytes * info->height) +
                            (offset & (PAGE_SIZE - 1)), PAGE_SIZE);
    uint32_t num_pages = size / PAGE_SIZE;
    uint32_t index;
    zx_paddr_t paddr[num_pages];
    mtx_lock(&canvas->lock);

    uint32_t height = info->height;
    uint32_t width  = info->stride_bytes;

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

    status = zx_bti_pin(canvas->bti, ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE,
                        vmo, offset & ~(PAGE_SIZE - 1), size,
                        paddr, num_pages,
                        &canvas->pmt_handle[index]);
    if (status != ZX_OK) {
        CANVAS_ERROR("zx_bti_pin failed %d \n", status);
        goto fail;
    }

    // check if all pages are contiguous
    for (uint32_t i = 0; i < num_pages - 1; i++) {
        if (paddr[i] + PAGE_SIZE != paddr[i + 1]) {
            CANVAS_ERROR("Pages are not contiguous\n");
            zx_handle_close(canvas->pmt_handle[index]);
            canvas->pmt_handle[index] = ZX_HANDLE_INVALID;
            status = ZX_ERR_INVALID_ARGS;
            goto fail;
        }
    }

    if (!IS_ALIGNED(paddr[0], 8)) {
        CANVAS_ERROR("Physical address is not aligned\n");
        status = ZX_ERR_INVALID_ARGS;
        zx_handle_close(canvas->pmt_handle[index]);
        canvas->pmt_handle[index] = ZX_HANDLE_INVALID;
        status = ZX_ERR_INVALID_ARGS;
        goto fail;
    }

    zx_paddr_t start_addr = paddr[0] + (offset & (PAGE_SIZE - 1));

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
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH,value);

    WRITE32_DMC_REG(DMC_CAV_LUT_ADDR, DMC_CAV_LUT_ADDR_WR_EN | index);

    // read a cbus to make sure last write finish.
    READ32_DMC_REG(DMC_CAV_LUT_DATAH);

    *canvas_idx = index;
fail:
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

static void aml_canvas_init(aml_canvas_t* canvas)
{
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAL, 0);
    WRITE32_DMC_REG(DMC_CAV_LUT_DATAH, 0);

    for (int index=0; index<NUM_CANVAS_ENTRIES; index++) {
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
    .unbind  = aml_canvas_unbind,
};

static canvas_protocol_ops_t canvas_ops = {
    .config = aml_canvas_config,
    .free   = aml_canvas_free,
};

static zx_status_t aml_canvas_bind(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    aml_canvas_t *canvas = calloc(1, sizeof(aml_canvas_t));
    if (!canvas) {
        return ZX_ERR_NO_MEMORY;
    }

    // Get device protocol
    status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_DEV, &canvas->pdev);
    if (status !=  ZX_OK) {
        CANVAS_ERROR("Could not get parent protocol\n");
        goto fail;
    }

    platform_bus_protocol_t pbus;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus)) != ZX_OK) {
        CANVAS_ERROR("ZX_PROTOCOL_PLATFORM_BUS not available %d \n",status);
        goto fail;
    }

    // Get BTI handle
    status = pdev_get_bti(&canvas->pdev, 0, &canvas->bti);
    if (status != ZX_OK) {
        CANVAS_ERROR("Could not get BTI handle\n");
        goto fail;
    }

    // Map all MMIOs
    status = pdev_map_mmio_buffer(&canvas->pdev, 0,
                                  ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                  &canvas->dmc_regs);
    if (status != ZX_OK) {
        CANVAS_ERROR("Could not map DMC registers %d\n",status);
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
        .proto_id = ZX_PROTOCOL_CANVAS,
    };

    status = device_add(parent, &args, &canvas->zxdev);
    if (status != ZX_OK) {
        goto fail;
    }

    canvas->canvas.ops = &canvas_ops;
    canvas->canvas.ctx = canvas;

    // Set the canvas protocol on the platform bus
    pbus_set_protocol(&pbus, ZX_PROTOCOL_CANVAS, &canvas->canvas);
    return ZX_OK;
fail:
    aml_canvas_release(canvas);
    return status;
}

static zx_driver_ops_t aml_canvas_driver_ops = {
    .version    = DRIVER_OPS_VERSION,
    .bind       = aml_canvas_bind,
};

ZIRCON_DRIVER_BEGIN(aml_canvas, aml_canvas_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CANVAS),
ZIRCON_DRIVER_END(aml_canvas)
