// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <lib/fidl/coding.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "fuchsia/display/c/fidl.h"
#include "lib/framebuffer/framebuffer.h"

static int32_t dc_fd = -1;
static zx_handle_t dc_handle = ZX_HANDLE_INVALID;

static int32_t txid;
static int32_t display_id;
static int32_t layer_id;

static int32_t width;
static int32_t height;
static int32_t stride;
static zx_pixel_format_t format;
static bool type_set;
static uint32_t image_type;

static zx_handle_t vmo = ZX_HANDLE_INVALID;

static bool inited = false;
static bool in_single_buffer_mode;

static zx_status_t set_layer_config(int32_t layer_id, uint32_t width, uint32_t height,
                                    zx_pixel_format_t format, int32_t type) {
    fuchsia_display_ControllerSetLayerPrimaryConfigRequest layer_cfg_msg;
    layer_cfg_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryConfigOrdinal;
    layer_cfg_msg.layer_id = layer_id;
    layer_cfg_msg.image_config.width = width;
    layer_cfg_msg.image_config.height = height;
    layer_cfg_msg.image_config.pixel_format = format;
    layer_cfg_msg.image_config.type = type;

    return zx_channel_write(dc_handle, 0, &layer_cfg_msg, sizeof(layer_cfg_msg), NULL, 0);
}

zx_status_t fb_bind(bool single_buffer, const char** err_msg_out) {
    const char* err_msg;
    if (!err_msg_out) {
        err_msg_out = &err_msg;
    }
    *err_msg_out = "";

    if (inited) {
        *err_msg_out = "framebufer already initialzied";
        return ZX_ERR_ALREADY_BOUND;
    }

    // TODO(stevensd): Don't hardcode display controller 0
    zx_status_t status;
    dc_fd = open("/dev/class/display-controller/000", O_RDWR);
    if (dc_fd < 0) {
        *err_msg_out = "Failed to open display controller";
        status = ZX_ERR_NO_RESOURCES;
        goto err;
    }

    if (ioctl_display_controller_get_handle(dc_fd, &dc_handle) != sizeof(zx_handle_t)) {
        *err_msg_out = "Failed to get display controller handle";
        status = ZX_ERR_INTERNAL;
        goto err;
    }

    zx_handle_t observed;
    uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    if ((status = zx_object_wait_one(dc_handle,
                                     signals, ZX_TIME_INFINITE, &observed)) != ZX_OK) {
        *err_msg_out = "Failed waiting for display";
        goto err;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
        *err_msg_out = "Display controller connection closed";
        status = ZX_ERR_PEER_CLOSED;
        goto err;
    }

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_read(dc_handle, 0, bytes, NULL, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        *err_msg_out = "Reading display addded callback failed";
        goto err;
    }

    if ((status = fidl_decode(&fuchsia_display_ControllerDisplaysChangedEventTable,
                              bytes, actual_bytes, NULL, 0, err_msg_out)) != ZX_OK) {
        goto err;
    }

    // We're guaranteed that added contains at least one display, since we haven't
    // been notified of any displays to remove.
    fuchsia_display_ControllerDisplaysChangedEvent* changes =
            (fuchsia_display_ControllerDisplaysChangedEvent*) bytes;
    fuchsia_display_Info* display = (fuchsia_display_Info*) changes->added.data;
    fuchsia_display_Mode* mode = (fuchsia_display_Mode*) display->modes.data;
    zx_pixel_format_t pixel_format = ((int32_t*)(display->pixel_format.data))[0];

    fuchsia_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = fuchsia_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.hdr.txid = txid++;
    stride_msg.width = mode->horizontal_resolution;
    stride_msg.pixel_format = pixel_format;

    fuchsia_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &stride_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        *err_msg_out = "Failed to get linear stride";
        goto err;
    }

    fuchsia_display_ControllerCreateLayerRequest create_layer_msg;
    create_layer_msg.hdr.ordinal = fuchsia_display_ControllerCreateLayerOrdinal;

    fuchsia_display_ControllerCreateLayerResponse create_layer_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &create_layer_msg;
    call_args.rd_bytes = &create_layer_rsp;
    call_args.wr_num_bytes = sizeof(create_layer_msg);
    call_args.rd_num_bytes = sizeof(create_layer_rsp);
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        *err_msg_out = "Create layer call failed";
        goto err;
    }
    if (create_layer_rsp.res != ZX_OK) {
        *err_msg_out = "Failed to create layer";
        status = create_layer_rsp.res;
        goto err;
    }

    uint8_t fidl_bytes[sizeof(fuchsia_display_ControllerSetDisplayLayersRequest)
                       + FIDL_ALIGN(sizeof(uint64_t))];
    fuchsia_display_ControllerSetDisplayLayersRequest* set_display_layer_request =
            (fuchsia_display_ControllerSetDisplayLayersRequest*) fidl_bytes;
    *(uint64_t*)(fidl_bytes + sizeof(fuchsia_display_ControllerSetDisplayLayersRequest)) =
            create_layer_rsp.layer_id;

    set_display_layer_request->hdr.ordinal = fuchsia_display_ControllerSetDisplayLayersOrdinal;
    set_display_layer_request->display_id = display->id;
    set_display_layer_request->layer_ids.count = 1;
    set_display_layer_request->layer_ids.data = (void*) FIDL_ALLOC_PRESENT;

    if ((status = zx_channel_write(dc_handle, 0, fidl_bytes,
                                   sizeof(fidl_bytes), NULL, 0)) != ZX_OK) {
        *err_msg_out = "Failed to set display layers";
        goto err;
    }

    if ((status = set_layer_config(create_layer_rsp.layer_id, mode->horizontal_resolution,
                                   mode->vertical_resolution, pixel_format,
                                   IMAGE_TYPE_SIMPLE)) != ZX_OK) {
        *err_msg_out = "Failed to set layer config";
        goto err;
    }

    display_id = display->id;
    layer_id = create_layer_rsp.layer_id;

    width = mode->horizontal_resolution;
    height = mode->vertical_resolution;
    format = pixel_format;
    stride = stride_rsp.stride;

    type_set = false;

    inited = true;

    if (single_buffer) {
        uint32_t size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);
        fuchsia_display_ControllerAllocateVmoRequest alloc_msg;
        alloc_msg.hdr.ordinal = fuchsia_display_ControllerAllocateVmoOrdinal;
        alloc_msg.hdr.txid = txid++;
        alloc_msg.size = size;

        fuchsia_display_ControllerAllocateVmoResponse alloc_rsp;
        zx_channel_call_args_t call_args = {};
        call_args.wr_bytes = &alloc_msg;
        call_args.rd_bytes = &alloc_rsp;
        call_args.rd_handles = &vmo;
        call_args.wr_num_bytes = sizeof(alloc_msg);
        call_args.rd_num_bytes = sizeof(alloc_rsp);
        call_args.rd_num_handles = 1;
        uint32_t actual_bytes, actual_handles;
        if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                      &actual_bytes, &actual_handles)) != ZX_OK) {
            *err_msg_out = "Failed vmo alloc call";
            goto err;
        }
        if (alloc_rsp.res != ZX_OK) {
            status = alloc_rsp.res;
            *err_msg_out = "Failed to alloc vmo";
            goto err;
        }

        // Failure to set the cache policy isn't a fatal error
        zx_vmo_set_cache_policy(vmo, ZX_CACHE_POLICY_WRITE_COMBINING);

        zx_handle_t dup;
        if ((status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
            *err_msg_out = "Couldn't duplicate vmo\n";;
            goto err;
        }

        // fb_(present|import)_image expect to not be in single buffer
        // mode, so make sure this is false for now. It will get set properly later.
        in_single_buffer_mode = false;

        uint64_t image_id;
        if ((status = fb_import_image(dup, 0, &image_id)) != ZX_OK) {
            *err_msg_out = "Couldn't import framebuffer";
            goto err;
        }

        if ((status = fb_present_image(image_id, -1, -1, -1)) != ZX_OK) {
            *err_msg_out = "Failed to present single_buffer mode framebuffer";
            goto err;
        }
    }

    in_single_buffer_mode = single_buffer;

    return ZX_OK;
err:
    inited = false;

    zx_handle_close(dc_handle);
    dc_handle = ZX_HANDLE_INVALID;

    if (dc_fd >= 0) {
        close(dc_fd);
        dc_fd = -1;
    }

    zx_handle_close(vmo);
    vmo = ZX_HANDLE_INVALID;

    return status;
}

void fb_release() {
    if (!inited) {
        return;
    }

    zx_handle_close(dc_handle);
    dc_handle = ZX_HANDLE_INVALID;

    close(dc_fd);
    dc_fd = -1;

    if (in_single_buffer_mode) {
        zx_handle_close(vmo);
        vmo = ZX_HANDLE_INVALID;
    }

    inited = false;
}

void fb_get_config(uint32_t* width_out, uint32_t* height_out,
                   uint32_t* linear_stride_px_out, zx_pixel_format_t* format_out) {
    ZX_ASSERT(inited);

    *width_out = width;
    *height_out = height;
    *format_out = format;
    *linear_stride_px_out = stride;
}

zx_handle_t fb_get_single_buffer() {
    ZX_ASSERT(inited && in_single_buffer_mode);
    return vmo;
}

zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t *id_out) {
    ZX_ASSERT(inited && !in_single_buffer_mode);
    zx_status_t status;

    if (type_set && type != image_type) {
        return ZX_ERR_BAD_STATE;
    } else if (!type_set && type != IMAGE_TYPE_SIMPLE) {
        if ((status = set_layer_config(layer_id, width, height, format, type)) != ZX_OK) {
            return status;
        }
        image_type = type;
        type_set = true;
    }

    fuchsia_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_display_ControllerImportVmoImageOrdinal;
    import_msg.hdr.txid = txid++;
    import_msg.image_config.height = height;
    import_msg.image_config.width = width;
    import_msg.image_config.pixel_format = format;
    import_msg.image_config.type = type;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;

    fuchsia_display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t import_call = {};
    import_call.wr_bytes = &import_msg;
    import_call.wr_handles = &handle;
    import_call.rd_bytes = &import_rsp;
    import_call.wr_num_bytes = sizeof(import_msg);
    import_call.wr_num_handles = 1;
    import_call.rd_num_bytes = sizeof(import_rsp);
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &import_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        return status;
    }

    if (import_rsp.res != ZX_OK) {
        return import_rsp.res;
    }

    *id_out = import_rsp.image_id;
    return ZX_OK;
}

void fb_release_image(uint64_t image_id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);

    fuchsia_display_ControllerReleaseEventRequest release_img_msg;
    release_img_msg.hdr.ordinal = fuchsia_display_ControllerReleaseEventOrdinal;
    release_img_msg.hdr.txid = txid++;
    release_img_msg.id = image_id;

    // There's nothing meaningful to do if this call fails
    zx_channel_write(dc_handle, 0, &release_img_msg, sizeof(release_img_msg), NULL, 0);
}

zx_status_t fb_import_event(zx_handle_t handle, uint64_t id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);

    fuchsia_display_ControllerImportEventRequest import_evt_msg;
    import_evt_msg.hdr.ordinal = fuchsia_display_ControllerImportEventOrdinal;
    import_evt_msg.hdr.txid = txid++;
    import_evt_msg.id = id;
    import_evt_msg.event = FIDL_HANDLE_PRESENT;

    return zx_channel_write(dc_handle, 0, &import_evt_msg, sizeof(import_evt_msg), &handle, 1);
}

void fb_release_event(uint64_t id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);

    fuchsia_display_ControllerReleaseEventRequest release_evt_msg;
    release_evt_msg.hdr.ordinal = fuchsia_display_ControllerReleaseEventOrdinal;
    release_evt_msg.hdr.txid = txid++;
    release_evt_msg.id = id;

    // There's nothing meaningful we can do if this call fails
    zx_channel_write(dc_handle, 0, &release_evt_msg, sizeof(release_evt_msg), NULL, 0);
}

zx_status_t fb_present_image2(uint64_t image_id, uint64_t wait_event_id, uint64_t signal_event_id) {
    return fb_present_image(image_id, wait_event_id, INVALID_ID, signal_event_id);
}

zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id,
                             uint64_t present_event_id, uint64_t signal_event_id) {
    ZX_ASSERT(present_event_id == INVALID_ID);
    ZX_ASSERT(inited && !in_single_buffer_mode);
    zx_status_t status;

    fuchsia_display_ControllerSetLayerImageRequest set_msg;
    set_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerImageOrdinal;
    set_msg.hdr.txid = txid++;
    set_msg.layer_id = layer_id;
    set_msg.image_id = image_id;
    set_msg.wait_event_id = wait_event_id;
    set_msg.signal_event_id = signal_event_id;
    if ((status = zx_channel_write(dc_handle, 0, &set_msg, sizeof(set_msg), NULL, 0)) != ZX_OK) {
        return status;
    }

    // It's not necessary to validate the configuration, since we're guaranteed that a single
    // fullscreen framebuffer on a single monitor will work.
    fuchsia_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.txid = txid++;
    apply_msg.hdr.ordinal = fuchsia_display_ControllerApplyConfigOrdinal;
    return zx_channel_write(dc_handle, 0, &apply_msg, sizeof(apply_msg), NULL, 0);
}

zx_status_t fb_alloc_image_buffer(zx_handle_t* vmo_out) {
    uint32_t size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);
    fuchsia_display_ControllerAllocateVmoRequest alloc_msg;
    alloc_msg.hdr.ordinal = fuchsia_display_ControllerAllocateVmoOrdinal;
    alloc_msg.hdr.txid = txid++;
    alloc_msg.size = size;

    fuchsia_display_ControllerAllocateVmoResponse alloc_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &alloc_msg;
    call_args.rd_bytes = &alloc_rsp;
    call_args.rd_handles = vmo_out;
    call_args.wr_num_bytes = sizeof(alloc_msg);
    call_args.rd_num_bytes = sizeof(alloc_rsp);
    call_args.rd_num_handles = 1;
    uint32_t actual_bytes, actual_handles;
    zx_status_t status;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        if (alloc_rsp.res != ZX_OK) {
            status = alloc_rsp.res;
        }
        return status;
    }
    return ZX_OK;
}
