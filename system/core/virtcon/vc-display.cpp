// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <port/port.h>
#include <fcntl.h>
#include <lib/fidl/coding.h>
#include <zircon/assert.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "fuchsia/display/c/fidl.h"
#include "vc.h"

static port_handler_t dc_ph;
static int dc_fd;

typedef struct display_info {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    zx_pixel_format_t format;

    uint64_t image_id;
    uint64_t layer_id;

    struct list_node node;
} display_info_t;

static struct list_node display_list = LIST_INITIAL_VALUE(display_list);

static bool displays_bound = false;
// Owned by vc_gfx, only valid when displays_bound is true
static zx_handle_t image_vmo = ZX_HANDLE_INVALID;
static fuchsia_display_ImageConfig image_config;

// remember whether the virtual console controls the display
bool g_vc_owns_display = false;

static zx_status_t vc_set_mode(uint8_t mode) {
    fuchsia_display_ControllerSetVirtconModeRequest request;
    request.hdr.ordinal = fuchsia_display_ControllerSetVirtconModeOrdinal;
    request.mode = mode;

    return zx_channel_write(dc_ph.handle, 0, &request, sizeof(request), nullptr, 0);
}

void vc_toggle_framebuffer() {
    if (list_is_empty(&display_list)) {
        return;
    }

    zx_status_t status = vc_set_mode(!g_vc_owns_display ?
            fuchsia_display_VirtconMode_FORCED : fuchsia_display_VirtconMode_FALLBACK);
    if (status != ZX_OK) {
        printf("vc: Failed to toggle ownership %d\n", status);
    }
}

static zx_status_t decode_message(void* bytes, uint32_t num_bytes) {
    fidl_message_header_t* header = (fidl_message_header_t*) bytes;

    if (num_bytes < sizeof(fidl_message_header_t)) {
        printf("vc: Unexpected short message (size=%d)\n", num_bytes);
        return ZX_ERR_INTERNAL;
    }
    zx_status_t res;

    const fidl_type_t* table = nullptr;
    if (header->ordinal == fuchsia_display_ControllerDisplaysChangedOrdinal) {
        table = &fuchsia_display_ControllerDisplaysChangedEventTable;
    } else if (header->ordinal == fuchsia_display_ControllerClientOwnershipChangeOrdinal) {
        table = &fuchsia_display_ControllerClientOwnershipChangeEventTable;
    }
    if (table != nullptr) {
        const char* err;
        if ((res = fidl_decode(table, bytes, num_bytes, nullptr, 0, &err)) != ZX_OK) {
            printf("vc: Error decoding message %d: %s\n", header->ordinal, err);
        }
    } else {
        printf("vc: Error unknown ordinal %d\n", header->ordinal);
        res = ZX_ERR_NOT_SUPPORTED;
    }
    return res;
}

static void handle_ownership_change(fuchsia_display_ControllerClientOwnershipChangeEvent* evt) {
    g_vc_owns_display = evt->has_ownership;

    // If we've gained it, repaint
    if (g_vc_owns_display && g_active_vc) {
        vc_flush_all(g_active_vc);
    }
}

static zx_status_t create_layer(uint64_t display_id, uint64_t* layer_id) {
    fuchsia_display_ControllerCreateLayerRequest create_layer_msg;
    create_layer_msg.hdr.ordinal = fuchsia_display_ControllerCreateLayerOrdinal;

    fuchsia_display_ControllerCreateLayerResponse create_layer_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &create_layer_msg;
    call_args.rd_bytes = &create_layer_rsp;
    call_args.wr_num_bytes = sizeof(create_layer_msg);
    call_args.rd_num_bytes = sizeof(create_layer_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Create layer call failed: %d (%s)\n", status, zx_status_get_string(status));
        return status;
    }
    if (create_layer_rsp.res != ZX_OK) {
        printf("vc: Failed to create layer %d\n", create_layer_rsp.res);
        return create_layer_rsp.res;
    }

    *layer_id = create_layer_rsp.layer_id;
    return ZX_OK;
}



static void destroy_layer(uint64_t layer_id) {
    fuchsia_display_ControllerDestroyLayerRequest destroy_msg;
    destroy_msg.hdr.ordinal = fuchsia_display_ControllerDestroyLayerOrdinal;
    destroy_msg.layer_id = layer_id;

    if (zx_channel_write(dc_ph.handle, 0, &destroy_msg, sizeof(destroy_msg), nullptr, 0) != ZX_OK) {
        printf("vc: Failed to destroy layer\n");
    }
}

static void release_image(uint64_t image_id) {
    fuchsia_display_ControllerReleaseImageRequest release_msg;
    release_msg.hdr.ordinal = fuchsia_display_ControllerReleaseImageOrdinal;
    release_msg.image_id = image_id;

    if (zx_channel_write(dc_ph.handle, 0, &release_msg, sizeof(release_msg), nullptr, 0)) {
        printf("vc: Failed to release image\n");
    }
}

static zx_status_t handle_display_added(fuchsia_display_Info* info, fuchsia_display_Mode* mode,
                                        int32_t pixel_format) {
    fuchsia_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = fuchsia_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.width = mode->horizontal_resolution;
    stride_msg.pixel_format = pixel_format;

    fuchsia_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    uint32_t actual_bytes, actual_handles;
    zx_status_t status;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &stride_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Failed to compute fb stride: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    if (stride_rsp.stride < mode->horizontal_resolution) {
        printf("vc: Got bad stride\n");
        return ZX_ERR_INVALID_ARGS;
    }

    display_info_t* display_info =
            reinterpret_cast<display_info_t*>(malloc(sizeof(display_info_t)));
    if (!display_info) {
        printf("vc: failed to alloc display info\n");
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = create_layer(info->id, &display_info->layer_id)) != ZX_OK) {
        printf("vc: failed to create display layer\n");
        free(display_info);
        return status;
    }

    display_info->id = info->id;
    display_info->width = mode->horizontal_resolution;
    display_info->height = mode->vertical_resolution;
    display_info->stride = stride_rsp.stride;
    display_info->format = reinterpret_cast<int32_t*>(info->pixel_format.data)[0];
    display_info->image_id = 0;

    list_add_tail(&display_list, &display_info->node);

    return ZX_OK;
}

static void handle_display_removed(uint64_t id) {
    if (list_is_empty(&display_list)) {
        printf("vc: No displays when removing %ld\n", id);
        return;
    }

    bool was_primary = list_peek_head_type(&display_list, display_info_t, node)->id == id;
    display_info_t* info = nullptr;
    display_info_t* temp = nullptr;
    list_for_every_entry_safe(&display_list, info, temp, display_info_t, node) {
        if (info->id == id) {
            destroy_layer(info->layer_id);
            release_image(info->image_id);

            list_delete(&info->node);
            free(info);
        } else if (was_primary) {
            release_image(info->image_id);
            info->image_id = 0;
        }
    }

    if (was_primary) {
        set_log_listener_active(false);
        vc_free_gfx();
        displays_bound = false;
    }
}

static zx_status_t allocate_vmo(uint32_t size, zx_handle_t* vmo_out) {
    fuchsia_display_ControllerAllocateVmoRequest alloc_msg;
    alloc_msg.hdr.ordinal = fuchsia_display_ControllerAllocateVmoOrdinal;
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
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Failed to alloc vmo: %d (%s)\n", status, zx_status_get_string(status));
        return status;
    }
    if (alloc_rsp.res != ZX_OK) {
        printf("vc: Failed to alloc vmo %d\n", alloc_rsp.res);
        return alloc_rsp.res;
    }
    return actual_handles == 1 ? ZX_OK : ZX_ERR_INTERNAL;
}

static zx_status_t import_vmo(zx_handle_t vmo, fuchsia_display_ImageConfig* config, uint64_t* id) {
    zx_handle_t vmo_dup;
    zx_status_t status;
    if ((status = zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &vmo_dup)) != ZX_OK) {
        printf("vc: Failed to dup fb handle %d\n", status);
        return status;
    }

    fuchsia_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_display_ControllerImportVmoImageOrdinal;
    import_msg.image_config = *config;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;

    fuchsia_display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &import_msg;
    call_args.wr_handles = &vmo_dup;
    call_args.rd_bytes = &import_rsp;
    call_args.wr_num_bytes = sizeof(import_msg);
    call_args.wr_num_handles = 1;
    call_args.rd_num_bytes = sizeof(import_rsp);
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Failed to import vmo call %d (%s)\n", status, zx_status_get_string(status));
        return status;
    }

    if (import_rsp.res != ZX_OK) {
        printf("vc: Failed to import vmo %d\n", import_rsp.res);
        return import_rsp.res;
    }

    *id = import_rsp.image_id;
    return ZX_OK;
}

static zx_status_t set_display_layer(uint64_t display_id, uint64_t layer_id) {
    zx_status_t status;
    // Put the layer on the display
    uint8_t fidl_bytes[sizeof(fuchsia_display_ControllerSetDisplayLayersRequest)
            + FIDL_ALIGN(sizeof(uint64_t))];
    auto set_display_layer_request =
            reinterpret_cast<fuchsia_display_ControllerSetDisplayLayersRequest*>(fidl_bytes);

    set_display_layer_request->hdr.ordinal = fuchsia_display_ControllerSetDisplayLayersOrdinal;
    set_display_layer_request->display_id = display_id;
    set_display_layer_request->layer_ids.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

    uint32_t size;
    if (layer_id) {
        set_display_layer_request->layer_ids.count = 1;
        *reinterpret_cast<uint64_t*>(set_display_layer_request + 1) = layer_id;
        size = sizeof(fidl_bytes);
    } else {
        set_display_layer_request->layer_ids.count = 0;
        size = sizeof(fuchsia_display_ControllerSetDisplayLayersRequest);
    }
    if ((status = zx_channel_write(dc_ph.handle, 0,
                                   fidl_bytes, size, nullptr, 0)) != ZX_OK) {
        printf("vc: Failed to set display layers %d\n", status);
        return status;
    }

    return ZX_OK;
}

static zx_status_t configure_layer(display_info_t* display, uint64_t layer_id,
                                   uint64_t image_id, fuchsia_display_ImageConfig* config) {
    zx_status_t status;
    fuchsia_display_ControllerSetLayerPrimaryConfigRequest layer_cfg_msg;
    layer_cfg_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryConfigOrdinal;
    layer_cfg_msg.layer_id = layer_id;
    layer_cfg_msg.image_config = *config;
    if ((status = zx_channel_write(dc_ph.handle, 0, &layer_cfg_msg,
                                   sizeof(layer_cfg_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Failed to set layer config %d\n", status);
        return status;
    }

    fuchsia_display_ControllerSetLayerPrimaryPositionRequest layer_pos_msg = {};
    layer_pos_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerPrimaryPositionOrdinal;
    layer_pos_msg.layer_id = display->id;
    layer_pos_msg.transform = fuchsia_display_Transform_IDENTITY;
    layer_pos_msg.src_frame.width = config->width;
    layer_pos_msg.src_frame.height = config->height;
    layer_pos_msg.dest_frame.width = display->width;
    layer_pos_msg.dest_frame.height = display->height;
    if ((status = zx_channel_write(dc_ph.handle, 0, &layer_pos_msg,
                                   sizeof(layer_pos_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Failed to set layer position %d\n", status);
        return status;
    }

    fuchsia_display_ControllerSetLayerImageRequest set_msg;
    set_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerImageOrdinal;
    set_msg.layer_id = layer_id;
    set_msg.image_id = image_id;
    if ((status = zx_channel_write(dc_ph.handle, 0,
                                   &set_msg, sizeof(set_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Failed to set image %d\n", status);
        return status;
    }
    return ZX_OK;
}

static zx_status_t apply_configuration() {
    // Validate and then apply the new configuration
    zx_status_t status;
    fuchsia_display_ControllerCheckConfigRequest check_msg;
    uint8_t check_rsp_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    auto check_rsp =
            reinterpret_cast<fuchsia_display_ControllerCheckConfigResponse*>(check_rsp_bytes);
    check_msg.discard = false;
    check_msg.hdr.ordinal = fuchsia_display_ControllerCheckConfigOrdinal;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &check_msg;
    call_args.rd_bytes = check_rsp;
    call_args.wr_num_bytes = sizeof(check_msg);
    call_args.rd_num_bytes = sizeof(check_rsp_bytes);
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_call(dc_ph.handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Failed to validate display config: %d (%s)\n", status,
               zx_status_get_string(status));
        return status;
    }

    if (check_rsp->res != fuchsia_display_ConfigResult_OK) {
        printf("vc: Config not valid %d\n", check_rsp->res);
        return ZX_ERR_INTERNAL;
    }

    fuchsia_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.ordinal = fuchsia_display_ControllerApplyConfigOrdinal;
    if ((status = zx_channel_write(dc_ph.handle, 0,
                                   &apply_msg, sizeof(apply_msg), nullptr, 0)) != ZX_OK) {
        printf("vc: Applying config failed %d\n", status);
        return status;
    }

    return ZX_OK;
}

static zx_status_t rebind_display(bool use_all) {
    // Arbitrarily pick the oldest display as the primary dispay
    display_info* primary = list_peek_head_type(&display_list, display_info, node);
    if (primary == nullptr) {
        printf("vc: No display to bind to\n");
        return ZX_ERR_NO_RESOURCES;
    }

    zx_status_t status;
    if (!displays_bound) {
        uint32_t size = primary->stride * primary->height * ZX_PIXEL_FORMAT_BYTES(primary->format);
        if ((status = allocate_vmo(size, &image_vmo)) != ZX_OK) {
            return ZX_ERR_NO_MEMORY;
        }
        image_config.height = primary->height;
        image_config.width = primary->width;
        image_config.pixel_format = primary->format;
        image_config.type = IMAGE_TYPE_SIMPLE;

        if ((status = vc_init_gfx(image_vmo, primary->width, primary->height,
                                  primary->format, primary->stride)) != ZX_OK) {
            printf("vc: failed to initialize graphics for new display %d\n", status);
            zx_handle_close(image_vmo);
            return status;
        }
    }

    display_info_t* info = nullptr;
    list_for_every_entry(&display_list, info, display_info_t, node) {
        if (!use_all && info != primary) {
            // If we're not showing anything on this display, remove its layer
            if ((status = set_display_layer(info->id, 0)) != ZX_OK) {
                break;
            }
        } else if (info->image_id == 0) {
            // If we want to display something but aren't, configure the display
            if ((status = import_vmo(image_vmo, &image_config, &info->image_id)) != ZX_OK) {
                break;
            }

            if ((status = set_display_layer(info->id, info->layer_id)) != ZX_OK) {
                break;
            }

            if ((status = configure_layer(info, info->layer_id,
                                          info->image_id, &image_config) != ZX_OK)) {
                break;
            }
        }
    }

    if (status == ZX_OK && apply_configuration() == ZX_OK) {
        // Only listen for logs when we have somewhere to print them. Also,
        // use a repeating wait so that we don't add/remove observers for each
        // log message (which is helpful when tracing the addition/removal of
        // observers).
        set_log_listener_active(true);
        vc_show_active();

        printf("vc: Successfully attached to display %ld\n", primary->id);
        displays_bound = true;
        return ZX_OK;
    } else {
        display_info_t* info = nullptr;
        list_for_every_entry(&display_list, info, display_info_t, node) {
            if (info->image_id) {
                release_image(info->image_id);
                info->image_id = 0;
            }
        }

        vc_free_gfx();

        if (use_all) {
            return rebind_display(false);
        } else {
            printf("vc: Failed to bind to displays\n");
            return ZX_ERR_INTERNAL;
        }
    }
}

static zx_status_t handle_display_changed(fuchsia_display_ControllerDisplaysChangedEvent* evt) {
    for (unsigned i = 0; i < evt->added.count; i++) {
        fuchsia_display_Info* info = reinterpret_cast<fuchsia_display_Info*>(evt->added.data) + i;
        fuchsia_display_Mode* mode = reinterpret_cast<fuchsia_display_Mode*>(info->modes.data);
        int32_t pixel_format = reinterpret_cast<int32_t*>(info->pixel_format.data)[0];
        zx_status_t status = handle_display_added(info, mode, pixel_format);
        if (status != ZX_OK) {
            return status;
        }
    }

    for (unsigned i = 0; i < evt->removed.count; i++) {
        handle_display_removed(reinterpret_cast<int32_t*>(evt->removed.data)[i]);
    }

    return rebind_display(true);
}

static zx_status_t dc_callback_handler(port_handler_t* ph, zx_signals_t signals, uint32_t evt) {
    if (signals & ZX_CHANNEL_PEER_CLOSED) {
        printf("vc: Displays lost\n");
        while (!list_is_empty(&display_list)) {
            handle_display_removed(list_peek_head_type(&display_list, display_info_t, node)->id);
        }

        close(dc_fd);
        zx_handle_close(dc_ph.handle);
        return ZX_ERR_STOP;
    }
    ZX_DEBUG_ASSERT(signals & ZX_CHANNEL_READABLE);

    zx_status_t status;
    uint32_t actual_bytes, actual_handles;
    uint8_t fidl_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
    if ((status = zx_channel_read(dc_ph.handle, 0,
                                  fidl_buffer, nullptr, ZX_CHANNEL_MAX_MSG_BYTES, 0,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        printf("vc: Error reading display message %d\n", status);
        return ZX_OK;
    }

    if (decode_message(fidl_buffer, actual_bytes) != ZX_OK) {
        return ZX_OK;
    }

    fidl_message_header_t* header = (fidl_message_header_t*) fidl_buffer;
    switch (header->ordinal) {
    case fuchsia_display_ControllerDisplaysChangedOrdinal: {
        handle_display_changed(
                reinterpret_cast<fuchsia_display_ControllerDisplaysChangedEvent*>(fidl_buffer));
        break;
    }
    case fuchsia_display_ControllerClientOwnershipChangeOrdinal: {
        auto evt = reinterpret_cast<fuchsia_display_ControllerClientOwnershipChangeEvent*>(
                fidl_buffer);
        handle_ownership_change(evt);
        break;
    }
    default:
        printf("vc: Unknown display callback message %d\n", header->ordinal);
        break;
    }

    return ZX_OK;
}

bool vc_display_init() {
    for (;;) {
        if ((dc_fd= open("/dev/class/display-controller/000/virtcon", O_RDWR)) >= 0) {
            break;
        }
        usleep(100000);
    }

    if (ioctl_display_controller_get_handle(dc_fd, &dc_ph.handle) != sizeof(zx_handle_t)) {
        printf("vc: failed to get display controller handle\n");
        return false;
    }

    zx_status_t status = vc_set_mode(getenv("virtcon.hide-on-boot") == nullptr ?
            fuchsia_display_VirtconMode_FALLBACK : fuchsia_display_VirtconMode_INACTIVE);
    if (status != ZX_OK) {
        printf("vc: Failed to set initial ownership %d\n", status);
        return false;
    }

    dc_ph.waitfor = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    dc_ph.func = dc_callback_handler;
    if ((status = port_wait(&port, &dc_ph)) != ZX_OK) {
        printf("vc: Failed to set port waiter %d\n", status);
        return false;
    }

    return true;
}
