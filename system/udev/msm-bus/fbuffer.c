// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/display.h>
#include <ddk/protocol/qcom.h>

#include <magenta/syscalls.h>

// clang-format on

static uint8_t* msm_framebuffer = (uint8_t*)NULL;
static uint32_t msm_framebuffer_size = 0;

static mx_device_t* disp_device;
static mx_display_info_t disp_info;

static mx_status_t msm_parse_framebuffer(char* args) {

    const char delim[] = ",";
    mx_status_t status = NO_ERROR;

    char* config;
    char* token;

    config = args;

    if ((token = strsep(&config, delim)) == NULL)
        return ERR_INVALID_ARGS;
    mx_paddr_t pa = strtol(token, NULL, 16);

    if ((token = strsep(&config, delim)) == NULL)
        return ERR_INVALID_ARGS;
    uint32_t width = strtol(token, NULL, 10);

    if ((token = strsep(&config, delim)) ==  NULL)
        return ERR_INVALID_ARGS;
    uint32_t height = strtol(token, NULL, 10);

    if ((token = strsep(&config, delim)) == NULL)
        return ERR_INVALID_ARGS;
    uint32_t bpp = strtol(token, NULL, 10);

    printf("MSMFBUFF: dimensions:%ux%u  bytesperpixel:%u\n", width, height, bpp);

    msm_framebuffer_size = width * height * bpp;

    uintptr_t page_base;

    // map framebuffer into userspace
    status = mx_mmap_device_memory(
        get_root_resource(),
        pa, msm_framebuffer_size,
        MX_CACHE_POLICY_CACHED, &page_base);
    msm_framebuffer = (uint8_t*)page_base;
    if (status != NO_ERROR)
        return status;

    memset(msm_framebuffer, 0x60, msm_framebuffer_size);

    printf("MSMFBUFF: fbuffer mapped at %p\n", msm_framebuffer);

    disp_info.format = MX_PIXEL_FORMAT_ARGB_8888;
    disp_info.width = width;
    disp_info.height = height;
    disp_info.stride = width;
    return 0;

fail:
    msm_framebuffer = NULL;
    return status;
}

void msm_flush_framebuffer(mx_device_t* dev) {
    mx_cache_flush(msm_framebuffer, msm_framebuffer_size,
                   MX_CACHE_FLUSH_DATA);
}

static mx_status_t msm_set_mode(mx_device_t* dev, mx_display_info_t* info) {

    return NO_ERROR;
}

static mx_status_t msm_get_mode(mx_device_t* dev, mx_display_info_t* info) {
    assert(info);
    memcpy(info, &disp_info, sizeof(mx_display_info_t));
    return NO_ERROR;
}

static mx_status_t msm_get_framebuffer(mx_device_t* dev, void** framebuffer) {
    assert(framebuffer);
    (*framebuffer) = msm_framebuffer;
    return NO_ERROR;
}

static mx_display_protocol_t msm_display_proto = {
    .set_mode = msm_set_mode,
    .get_mode = msm_get_mode,
    .get_framebuffer = msm_get_framebuffer,
    .flush = msm_flush_framebuffer};

static mx_protocol_device_t msm_device_proto = {};

mx_status_t fb_bind(mx_driver_t* driver, mx_device_t* parent, void** cookie) {

    mx_status_t status;
    char* v = getenv("magenta.fbuffer");

    if (v == NULL)
        return ERR_INVALID_ARGS;

    status = msm_parse_framebuffer(v);
    if (status != NO_ERROR)
        return status;
    status = mx_set_framebuffer(get_root_resource(), msm_framebuffer,
                                msm_framebuffer_size, disp_info.format,
                                disp_info.width, disp_info.height, disp_info.stride);
    if (status != NO_ERROR) {
        return status;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "msm-fb",
        .driver = driver,
        .ops = &msm_device_proto,
        .proto_id = MX_PROTOCOL_DISPLAY,
        .proto_ops = &msm_display_proto,
    };

    return device_add2(parent, &args, &disp_device);
}

static mx_driver_ops_t msm_fb_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = fb_bind,
};

// clang-format off
MAGENTA_DRIVER_BEGIN(msm_fb, msm_fb_driver_ops, "magenta", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, MX_PROTOCOL_SOC),
    BI_ABORT_IF(NE, BIND_SOC_VID, SOC_VID_QCOM),
    BI_MATCH_IF(EQ, BIND_SOC_PID, SOC_PID_TRAPPER),
MAGENTA_DRIVER_END(msm_fb)
