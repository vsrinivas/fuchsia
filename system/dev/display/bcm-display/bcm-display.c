// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/bcm-bus.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/platform-device.h>

#include <magenta/syscalls.h>
#include <magenta/assert.h>

#include <bcm/bcm28xx.h>
#include <bcm/ioctl.h>

typedef struct {
    uint32_t phys_width;    //request
    uint32_t phys_height;   //request
    uint32_t virt_width;    //request
    uint32_t virt_height;   //request
    uint32_t pitch;         //response
    uint32_t depth;         //request
    uint32_t virt_x_offs;   //request
    uint32_t virt_y_offs;   //request
    uint32_t fb_p;          //response
    uint32_t fb_size;       //response
} bcm_fb_desc_t;

typedef struct {
    bcm_bus_protocol_t bus_proto;
    mx_display_info_t disp_info;
    bcm_fb_desc_t fb_desc;
    io_buffer_t buffer;
    uint8_t* framebuffer;
} bcm_display_t;

static mx_status_t vc_set_mode(void* ctx, mx_display_info_t* info) {
    return MX_OK;
}

static mx_status_t vc_get_mode(void* ctx, mx_display_info_t* info) {
    if (!info) return MX_ERR_INVALID_ARGS;
    bcm_display_t* display = ctx;
    memcpy(info, &display->disp_info, sizeof(mx_display_info_t));
    return MX_OK;
}

static mx_status_t vc_get_framebuffer(void* ctx, void** framebuffer) {
    if (!framebuffer) return MX_ERR_INVALID_ARGS;
    bcm_display_t* display = ctx;
    (*framebuffer) = display->framebuffer;
    return MX_OK;
}

static void vc_flush_framebuffer(void* ctx) {
    bcm_display_t* display = ctx;
    mx_cache_flush(display->framebuffer, display->fb_desc.fb_size, MX_CACHE_FLUSH_DATA);
}

static display_protocol_ops_t vc_display_proto = {
    .set_mode = vc_set_mode,
    .get_mode = vc_get_mode,
    .get_framebuffer = vc_get_framebuffer,
    .flush = vc_flush_framebuffer
};

static mx_protocol_device_t empty_device_proto = {
    .version = DEVICE_OPS_VERSION,
};

static mx_status_t bcm_vc_get_framebuffer(bcm_display_t* display, bcm_fb_desc_t* fb_desc) {
    mx_status_t ret = MX_OK;
    iotxn_t* txn;

    if (!display->framebuffer) {
        // buffer needs to be aligned on 16 byte boundary, pad the alloc to make sure we have room to adjust
        const size_t txnsize = sizeof(bcm_fb_desc_t) + 16;
        ret = iotxn_alloc(&txn, IOTXN_ALLOC_CONTIGUOUS | IOTXN_ALLOC_POOL, txnsize);
        if (ret < 0)
            return ret;

        iotxn_physmap(txn);
        MX_DEBUG_ASSERT(txn->phys_count == 1);
        mx_paddr_t phys = iotxn_phys(txn);

        // calculate offset in buffer that will provide 16 byte alignment (physical)
        uint32_t offset = (16 - (phys % 16)) % 16;

        iotxn_copyto(txn, fb_desc, sizeof(bcm_fb_desc_t), offset);
        iotxn_cacheop(txn, IOTXN_CACHE_CLEAN, 0, txnsize);

        ret = bcm_bus_set_framebuffer(&display->bus_proto, phys + offset);
        if (ret != MX_OK) {
            printf("bcm_bus_set_framebuffer failed %d\n", ret);
            return ret;
        }

        iotxn_cacheop(txn, IOTXN_CACHE_INVALIDATE, 0, txnsize);
        iotxn_copyfrom(txn, &display->fb_desc, sizeof(bcm_fb_desc_t), offset);

        // map framebuffer into userspace
        ret = io_buffer_init_physical(&display->buffer, display->fb_desc.fb_p & 0x3fffffff,
                                      display->fb_desc.fb_size, get_root_resource(),
                                      MX_CACHE_POLICY_CACHED);
        iotxn_release(txn);

        if (ret != MX_OK) {
            printf("bcm_vc_get_framebuffer: io_buffer_init_physical failed %d\n", ret);
            return ret;
        }
        display->framebuffer = (uint8_t*)io_buffer_virt(&display->buffer);
        memset(display->framebuffer, 0x00, display->fb_desc.fb_size);
    }
    memcpy(fb_desc, &display->fb_desc, sizeof(bcm_fb_desc_t));
    return MX_OK;
}

mx_status_t bcm_display_bind(void* ctx, mx_device_t* parent, void** cookie) {
    bcm_display_t* display = calloc(1, sizeof(bcm_display_t));
    if (!display) {
        return MX_ERR_NO_MEMORY;
    }

   platform_device_protocol_t pdev;
    mx_status_t status = device_get_protocol(parent, MX_PROTOCOL_PLATFORM_DEV, &pdev);
    if (status !=  MX_OK) {
        free(display);
        return status;
    }

    status = pdev_get_protocol(&pdev, MX_PROTOCOL_BCM_BUS, &display->bus_proto);
    if (status != MX_OK) {
        printf("bcm_display_bind can't find MX_PROTOCOL_BCM_BUS\n");
        free(display);
        return status;
    }

    bcm_fb_desc_t framebuff_descriptor;

    // For now these are set to work with the rpi 5" lcd didsplay
    // TODO: add a mechanisms to specify and change settings outside the driver

    framebuff_descriptor.phys_width = 800;
    framebuff_descriptor.phys_height = 480;
    framebuff_descriptor.virt_width = 800;
    framebuff_descriptor.virt_height = 480;
    framebuff_descriptor.pitch = 0;
    framebuff_descriptor.depth = 32;
    framebuff_descriptor.virt_x_offs = 0;
    framebuff_descriptor.virt_y_offs = 0;
    framebuff_descriptor.fb_p = 0;
    framebuff_descriptor.fb_size = 0;

    if (bcm_vc_get_framebuffer(display, &framebuff_descriptor) != MX_OK) {
        free(display);
        return status;
    }

    display->disp_info.format = MX_PIXEL_FORMAT_ARGB_8888;
    display->disp_info.width = 800;
    display->disp_info.height = 480;
    display->disp_info.stride = 800;

    mx_set_framebuffer(get_root_resource(), display->framebuffer,
                       display->fb_desc.fb_size, display->disp_info.format,
                       display->disp_info.width, display->disp_info.height,
                       display->disp_info.stride);

    device_add_args_t vc_fbuff_args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "bcm-vc-fbuff",
        .ctx = display,
        .ops = &empty_device_proto,
        .proto_id = MX_PROTOCOL_DISPLAY,
        .proto_ops = &vc_display_proto,
    };

    status = device_add(parent, &vc_fbuff_args, NULL);
    if (status != MX_OK) {
        free(display);
    }
    return status;
}

static mx_driver_ops_t bcm_display_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = bcm_display_bind,
};

MAGENTA_DRIVER_BEGIN(bcm_display, bcm_display_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_BROADCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_BROADCOMM_DISPLAY),
MAGENTA_DRIVER_END(bcm_display)
