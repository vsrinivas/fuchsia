// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <lib/fidl/coding.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/pixelformat.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "fuchsia/hardware/display/c/fidl.h"
#include "lib/framebuffer/framebuffer.h"

static zx_handle_t device_handle = ZX_HANDLE_INVALID;
static zx_handle_t dc_handle = ZX_HANDLE_INVALID;

static int32_t txid;
static uint64_t display_id;
static uint64_t layer_id;

static int32_t width;
static int32_t height;
static int32_t stride;
static zx_pixel_format_t format;
static bool type_set;
static uint32_t image_type;

static zx_handle_t vmo = ZX_HANDLE_INVALID;

static bool inited = false;
static bool in_single_buffer_mode;

static zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t* id_out);
static void fb_release_image(uint64_t id);

// Imports an event handle to use for image synchronization. This function
// always consumes |handle|. Id must be unique and not equal to FB_INVALID_ID.
static zx_status_t fb_import_event(zx_handle_t handle, uint64_t id);
static void fb_release_event(uint64_t id);

// Presents the image identified by |image_id|.
//
// If |wait_event_id| corresponds to an imported event, then driver will wait for
// for ZX_EVENT_SIGNALED before using the buffer. If |signal_event_id| corresponds
// to an imported event, then the driver will signal ZX_EVENT_SIGNALED when it is
// done with the image.
static zx_status_t fb_present_image(uint64_t image_id,
                                    uint64_t wait_event_id, uint64_t signal_event_id);

static zx_status_t set_layer_config(uint64_t layer_id, uint32_t width, uint32_t height,
                                    zx_pixel_format_t format, int32_t type) {
    fuchsia_hardware_display_ControllerSetLayerPrimaryConfigRequest layer_cfg_msg = {};
    layer_cfg_msg.hdr.ordinal = fuchsia_hardware_display_ControllerSetLayerPrimaryConfigOrdinal;
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
    fbl::unique_fd dc_fd(open("/dev/class/display-controller/000", O_RDWR));
    if (!dc_fd) {
        *err_msg_out = "Failed to open display controller";
        return ZX_ERR_NO_RESOURCES;
    }

    zx::channel device_server, device_client;
    zx_status_t status = zx::channel::create(0, &device_server, &device_client);
    if (status != ZX_OK) {
        *err_msg_out = "Failed to create device channel";
        return status;
    }

    zx::channel dc_server, dc_client;
    status = zx::channel::create(0, &dc_server, &dc_client);
    if (status != ZX_OK) {
        *err_msg_out = "Failed to create controller channel";
        return status;
    }

    fzl::FdioCaller caller(std::move(dc_fd));
    zx_status_t fidl_status = fuchsia_hardware_display_ProviderOpenController(
        caller.borrow_channel(), device_server.release(), dc_server.release(), &status);
    if (fidl_status != ZX_OK) {
        *err_msg_out = "Failed to call service handle";
        return fidl_status;
    }
    if (status != ZX_OK) {
        *err_msg_out = "Failed to open controller";
        return status;
    }

    device_handle = device_client.release();
    dc_handle = dc_client.release();
    fbl::AutoCall close_dc_handle([]() {
        zx_handle_close(device_handle);
        zx_handle_close(dc_handle);
        device_handle = ZX_HANDLE_INVALID;
        dc_handle = ZX_HANDLE_INVALID;
    });

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes, actual_handles;
    bool has_display = false;
    do {
        zx_handle_t observed;
        uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
        if ((status = zx_object_wait_one(dc_handle,
                                         signals, ZX_TIME_INFINITE, &observed)) != ZX_OK) {
            *err_msg_out = "Failed waiting for display";
            return status;
        }
        if (observed & ZX_CHANNEL_PEER_CLOSED) {
            *err_msg_out = "Display controller connection closed";
            return ZX_ERR_PEER_CLOSED;
        }

        if ((status = zx_channel_read(dc_handle, 0, bytes, NULL, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                      &actual_bytes, &actual_handles)) != ZX_OK ||
            actual_bytes < sizeof(fidl_message_header_t)) {
            *err_msg_out = "Reading display addded callback failed";
            return status;
        }

        fidl_message_header_t* hdr = (fidl_message_header_t*)bytes;
        if (hdr->ordinal == fuchsia_hardware_display_ControllerDisplaysChangedOrdinal) {
            if ((status = fidl_decode(&fuchsia_hardware_display_ControllerDisplaysChangedEventTable,
                                      bytes, actual_bytes, NULL, 0, err_msg_out)) != ZX_OK) {
                return status;
            }
            has_display = true;
        }
    } while (!has_display);

    // We're guaranteed that added contains at least one display, since we haven't
    // been notified of any displays to remove.
    fuchsia_hardware_display_ControllerDisplaysChangedEvent* changes =
        (fuchsia_hardware_display_ControllerDisplaysChangedEvent*)bytes;
    fuchsia_hardware_display_Info* display = (fuchsia_hardware_display_Info*)changes->added.data;
    fuchsia_hardware_display_Mode* mode = (fuchsia_hardware_display_Mode*)display->modes.data;
    zx_pixel_format_t pixel_format = ((int32_t*)(display->pixel_format.data))[0];

    fuchsia_hardware_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = fuchsia_hardware_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.hdr.txid = txid++;
    stride_msg.width = mode->horizontal_resolution;
    stride_msg.pixel_format = pixel_format;

    fuchsia_hardware_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &stride_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        *err_msg_out = "Failed to get linear stride";
        return status;
    }

    fuchsia_hardware_display_ControllerCreateLayerRequest create_layer_msg;
    create_layer_msg.hdr.ordinal = fuchsia_hardware_display_ControllerCreateLayerOrdinal;

    fuchsia_hardware_display_ControllerCreateLayerResponse create_layer_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &create_layer_msg;
    call_args.rd_bytes = &create_layer_rsp;
    call_args.wr_num_bytes = sizeof(create_layer_msg);
    call_args.rd_num_bytes = sizeof(create_layer_rsp);
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        *err_msg_out = "Create layer call failed";
        return status;
    }
    if (create_layer_rsp.res != ZX_OK) {
        *err_msg_out = "Failed to create layer";
        status = create_layer_rsp.res;
        return status;
    }

    uint8_t fidl_bytes[sizeof(fuchsia_hardware_display_ControllerSetDisplayLayersRequest) +
                       FIDL_ALIGN(sizeof(uint64_t))];
    fuchsia_hardware_display_ControllerSetDisplayLayersRequest* set_display_layer_request =
        (fuchsia_hardware_display_ControllerSetDisplayLayersRequest*)fidl_bytes;
    *(uint64_t*)(fidl_bytes + sizeof(fuchsia_hardware_display_ControllerSetDisplayLayersRequest)) =
        create_layer_rsp.layer_id;

    set_display_layer_request->hdr.ordinal =
        fuchsia_hardware_display_ControllerSetDisplayLayersOrdinal;
    set_display_layer_request->display_id = display->id;
    set_display_layer_request->layer_ids.count = 1;
    set_display_layer_request->layer_ids.data = (void*)FIDL_ALLOC_PRESENT;

    if ((status = zx_channel_write(dc_handle, 0, fidl_bytes,
                                   sizeof(fidl_bytes), NULL, 0)) != ZX_OK) {
        *err_msg_out = "Failed to set display layers";
        return status;
    }

    if ((status = set_layer_config(create_layer_rsp.layer_id, mode->horizontal_resolution,
                                   mode->vertical_resolution, pixel_format,
                                   IMAGE_TYPE_SIMPLE)) != ZX_OK) {
        *err_msg_out = "Failed to set layer config";
        return status;
    }

    display_id = display->id;
    layer_id = create_layer_rsp.layer_id;

    width = mode->horizontal_resolution;
    height = mode->vertical_resolution;
    format = pixel_format;
    stride = stride_rsp.stride;

    type_set = false;

    inited = true;

    fbl::AutoCall clear_inited([]() {
        inited = false;
    });

    zx::vmo local_vmo;

    if (single_buffer) {
        uint32_t size = stride * height * ZX_PIXEL_FORMAT_BYTES(format);
        fuchsia_hardware_display_ControllerAllocateVmoRequest alloc_msg;
        alloc_msg.hdr.ordinal = fuchsia_hardware_display_ControllerAllocateVmoOrdinal;
        alloc_msg.hdr.txid = txid++;
        alloc_msg.size = size;

        fuchsia_hardware_display_ControllerAllocateVmoResponse alloc_rsp;
        zx_channel_call_args_t call_args = {};
        call_args.wr_bytes = &alloc_msg;
        call_args.rd_bytes = &alloc_rsp;
        call_args.rd_handles = local_vmo.reset_and_get_address();
        call_args.wr_num_bytes = sizeof(alloc_msg);
        call_args.rd_num_bytes = sizeof(alloc_rsp);
        call_args.rd_num_handles = 1;
        uint32_t actual_bytes, actual_handles;
        if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                      &actual_bytes, &actual_handles)) != ZX_OK) {
            *err_msg_out = "Failed vmo alloc call";
            return status;
        }
        if (alloc_rsp.res != ZX_OK) {
            status = alloc_rsp.res;
            *err_msg_out = "Failed to alloc vmo";
            return status;
        }

        // Failure to set the cache policy isn't a fatal error
        zx_vmo_set_cache_policy(local_vmo.get(), ZX_CACHE_POLICY_WRITE_COMBINING);

        zx_handle_t dup;
        if ((status = zx_handle_duplicate(local_vmo.get(), ZX_RIGHT_SAME_RIGHTS, &dup)) != ZX_OK) {
            *err_msg_out = "Couldn't duplicate vmo\n";
            return status;
        }

        // fb_(present|import)_image expect to not be in single buffer
        // mode, so make sure this is false for now. It will get set properly later.
        in_single_buffer_mode = false;

        uint64_t image_id;
        if ((status = fb_import_image(dup, 0, &image_id)) != ZX_OK) {
            *err_msg_out = "Couldn't import framebuffer";
            return status;
        }

        if ((status = fb_present_image(image_id, INVALID_ID, INVALID_ID)) != ZX_OK) {
            *err_msg_out = "Failed to present single_buffer mode framebuffer";
            return status;
        }
    }

    in_single_buffer_mode = single_buffer;

    clear_inited.cancel();
    vmo = local_vmo.release();
    close_dc_handle.cancel();

    return ZX_OK;
}

void fb_release() {
    if (!inited) {
        return;
    }

    zx_handle_close(device_handle);
    zx_handle_close(dc_handle);
    device_handle = ZX_HANDLE_INVALID;
    dc_handle = ZX_HANDLE_INVALID;

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

zx_status_t fb_import_image(zx_handle_t handle, uint32_t type, uint64_t* id_out) {
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

    fuchsia_hardware_display_ControllerImportVmoImageRequest import_msg = {};
    import_msg.hdr.ordinal = fuchsia_hardware_display_ControllerImportVmoImageOrdinal;
    import_msg.hdr.txid = txid++;
    import_msg.image_config.height = height;
    import_msg.image_config.width = width;
    import_msg.image_config.pixel_format = format;
    import_msg.image_config.type = type;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;

    fuchsia_hardware_display_ControllerImportVmoImageResponse import_rsp;
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

    fuchsia_hardware_display_ControllerReleaseEventRequest release_img_msg;
    release_img_msg.hdr.ordinal = fuchsia_hardware_display_ControllerReleaseEventOrdinal;
    release_img_msg.hdr.txid = txid++;
    release_img_msg.id = image_id;

    // There's nothing meaningful to do if this call fails
    zx_channel_write(dc_handle, 0, &release_img_msg, sizeof(release_img_msg), NULL, 0);
}

zx_status_t fb_import_event(zx_handle_t handle, uint64_t id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);

    fuchsia_hardware_display_ControllerImportEventRequest import_evt_msg;
    import_evt_msg.hdr.ordinal = fuchsia_hardware_display_ControllerImportEventOrdinal;
    import_evt_msg.hdr.txid = txid++;
    import_evt_msg.id = id;
    import_evt_msg.event = FIDL_HANDLE_PRESENT;

    return zx_channel_write(dc_handle, 0, &import_evt_msg, sizeof(import_evt_msg), &handle, 1);
}

void fb_release_event(uint64_t id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);

    fuchsia_hardware_display_ControllerReleaseEventRequest release_evt_msg;
    release_evt_msg.hdr.ordinal = fuchsia_hardware_display_ControllerReleaseEventOrdinal;
    release_evt_msg.hdr.txid = txid++;
    release_evt_msg.id = id;

    // There's nothing meaningful we can do if this call fails
    zx_channel_write(dc_handle, 0, &release_evt_msg, sizeof(release_evt_msg), NULL, 0);
}

zx_status_t fb_present_image(uint64_t image_id, uint64_t wait_event_id,
                             uint64_t signal_event_id) {
    ZX_ASSERT(inited && !in_single_buffer_mode);
    zx_status_t status;

    fuchsia_hardware_display_ControllerSetLayerImageRequest set_msg;
    set_msg.hdr.ordinal = fuchsia_hardware_display_ControllerSetLayerImageOrdinal;
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
    fuchsia_hardware_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.txid = txid++;
    apply_msg.hdr.ordinal = fuchsia_hardware_display_ControllerApplyConfigOrdinal;
    return zx_channel_write(dc_handle, 0, &apply_msg, sizeof(apply_msg), NULL, 0);
}

zx_status_t fb_enable_vsync(bool enable) {
    fuchsia_hardware_display_ControllerEnableVsyncRequest enable_vsync;
    enable_vsync.hdr.ordinal = fuchsia_hardware_display_ControllerEnableVsyncOrdinal;
    enable_vsync.enable = enable;
    zx_status_t status;
    if ((status = zx_channel_write(dc_handle, 0, &enable_vsync, sizeof(enable_vsync),
                                   NULL, 0)) != ZX_OK) {
        return status;
    }
    return ZX_OK;
}

zx_status_t fb_wait_for_vsync(zx_time_t* timestamp, uint64_t* image_id) {
    zx_status_t status;

    zx_handle_t observed;
    uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    if ((status = zx_object_wait_one(dc_handle, signals, ZX_TIME_INFINITE,
                                     &observed)) != ZX_OK) {
        return status;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
        return ZX_ERR_PEER_CLOSED;
    }

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_read(dc_handle, 0, bytes, NULL, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        return ZX_ERR_STOP;
    }

    if (actual_bytes < sizeof(fidl_message_header_t)) {
        return ZX_ERR_INTERNAL;
    }

    fidl_message_header_t* header = (fidl_message_header_t*)bytes;

    switch (header->ordinal) {
    case fuchsia_hardware_display_ControllerDisplaysChangedOrdinal:
        return ZX_ERR_STOP;
    case fuchsia_hardware_display_ControllerClientOwnershipChangeOrdinal:
        return ZX_ERR_NEXT;
    case fuchsia_hardware_display_ControllerVsyncOrdinal:
        break;
    default:
        return ZX_ERR_STOP;
    }

    const char* err_msg;
    if ((status = fidl_decode(&fuchsia_hardware_display_ControllerVsyncEventTable, bytes,
                              actual_bytes, NULL, 0, &err_msg)) != ZX_OK) {
        return ZX_ERR_STOP;
    }

    fuchsia_hardware_display_ControllerVsyncEvent* vsync =
        (fuchsia_hardware_display_ControllerVsyncEvent*)bytes;
    *timestamp = vsync->timestamp;
    *image_id = vsync->images.count ? *((uint64_t*)vsync->images.data) : FB_INVALID_ID;
    return ZX_OK;
}
