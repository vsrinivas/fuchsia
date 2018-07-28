// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fcntl.h>
#include <gfx/gfx.h>
#include <hid/paradise.h>
#include <hid/usages.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/coding.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <unistd.h>
#include <zircon/device/display-controller.h>
#include <zircon/device/input.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "fuchsia/display/c/fidl.h"

#define DEV_INPUT "/dev/class/input"
#define NUM_FINGERS 5u
#define STYLUS_PEN 0
#define TOUCH_PEN 1
#define NUM_PENCILS 2
#define NUM_BUFFERS 2u
#define SPRITE_DIM 256u
#define SPRITE_RAD (SPRITE_DIM / 2)
#define SPRITE_FORMAT ZX_PIXEL_FORMAT_ARGB_8888
#define PEN_VELOCITY_MAX 12.5f
// Interpolation factor used to compute responsive velocity. Valid range
// is 0.0 to 1.0, where 1.0 takes only current velocity into account.
#define RESPONSIVE_VELOCITY_FACTOR 0.75
// Interpolation factor used to compute smooth velocity. Valid range
// is 0.0 to 1.0, where 1.0 takes only current velocity into account.
#define SMOOTH_VELOCITY_FACTOR 0.25
// Interpolation factor used to compute pen movement. Valid range
// is 0.0 to 1.0, where 1.0 takes only smooth velocity into account.
#define PEN_MOVEMENT_FACTOR 0.25
#define MAX_BLUR_RADIUS 90.0
#define MIN_MOVEMENT_FOR_CURSOR_MOTION_BLUR 2.0
#define CURSOR_MOVEMENT_PREDICTION_MS (1000.0f / 60.0f)
#define CURSOR_HOTSPOT_X 15
#define CURSOR_HOTSPOT_Y 14
#define ORIGIN_VELOCITY_MAX 10.0f
#define ORIGIN_MOVEMENT_FACTOR 0.9f
// Input prediction models depend on velocity which need to be sampled
// at a fixed interval. For example, the lack of input over the interval
// affects the model. Note: This is currently set to cause an update
// for each frame when VSync is enabled.
#define INPUT_PREDICTION_UPDATE_INTERVAL_MS 16

enum class VSync {
    ON,
    OFF,
    ADAPTIVE,
};

typedef struct point {
    uint32_t x;
    uint32_t y;
} point_t;

typedef struct line {
    point_t p1;
    point_t p2;
} line_t;

typedef struct vector {
    int32_t x;
    int32_t y;
} vector_t;

typedef struct pointf {
    float x;
    float y;
} pointf_t;

typedef struct vectorf {
    float x;
    float y;
} vectorf_t;

typedef struct rect {
    uint32_t x1;
    uint32_t y1;
    uint32_t x2;
    uint32_t y2;
} rect_t;

typedef struct buffer {
    zx_handle_t vmo;
    uintptr_t data;
    uint64_t image_id;
    zx_handle_t wait_event;
    uint64_t wait_event_id;
    rect_t damage;
} buffer_t;

static zx_handle_t dc_handle = ZX_HANDLE_INVALID;
static int32_t txid = 0;

#include "background.inc"
#include "cursor.inc"

static double scale(double z, uint32_t screen_dim, uint32_t rpt_dim) {
    return (z * screen_dim) / rpt_dim;
}

static void vector_interpolate(vectorf_t* result, const vectorf_t* start,
                               const vectorf_t* end, float f) {

    result->x = start->x + (end->x - start->x) * f;
    result->y = start->y + (end->y - start->y) * f;
}

static void copy_rect(uint32_t* dst, const uint32_t* src, uint32_t dst_stride,
                      uint32_t src_stride, uint32_t x1, uint32_t y1,
                      uint32_t x2, uint32_t y2) {
    size_t bytes_per_line = (x2 - x1) * sizeof(uint32_t);
    size_t lines = y2 - y1;

    dst += y1 * dst_stride + x1;
    src += y1 * src_stride + x1;

    while (lines--) {
        memcpy(dst, src, bytes_per_line);
        dst += dst_stride;
        src += src_stride;
    }
}

/* source dimensions must be a power of two. */
static void rotate_rect(uint32_t* dst, const uint32_t* src, uint32_t dst_width,
                        uint32_t dst_height, uint32_t dst_stride,
                        uint32_t src_width, uint32_t src_height,
                        uint32_t src_stride, double dst_cx, double dst_cy,
                        double src_cx, double src_cy, double angle) {
    ZX_ASSERT(fbl::is_pow2(src_width));
    ZX_ASSERT(fbl::is_pow2(src_height));

    double du_y = sin(-angle);
    double dv_y = cos(-angle);
    double du_x = dv_y;
    double dv_x = -du_y;
    double startu = src_cx - (dst_cx * dv_y + dst_cy * du_y);
    double startv = src_cy - (dst_cx * dv_x + dst_cy * du_x);
    double rowu = startu;
    double rowv = startv;
    int width_mask = src_width - 1;
    int height_mask = src_height - 1;

    for (uint32_t y = 0; y < dst_height; y++) {
        uint32_t* d = dst + y * dst_stride;
        double u = rowu;
        double v = rowv;

        for (uint32_t x = 0; x < dst_width; x++) {
            const uint32_t* s = src + ((((int)v) & height_mask) * src_stride) +
                                (((int)u) & width_mask);

            *d++ = *s++;

            u += du_x;
            v += dv_x;
        }

        rowu += du_y;
        rowv += dv_y;
    }
}

static inline uint8_t mul_div_255_round(uint16_t a, uint16_t b) {
    unsigned prod = a * b + 128;
    return (uint8_t)((prod + (prod >> 8)) >> 8);
}

static inline void argb_8888_unpack_mul(uint32_t p, uint8_t* a, uint8_t* r,
                                        uint8_t* g, uint8_t* b) {
    *a = (uint8_t)((p & 0xff000000) >> 24);
    *r = (uint8_t)((p & 0x00ff0000) >> 16);
    *g = (uint8_t)((p & 0x0000ff00) >> 8);
    *b = (uint8_t)(p & 0x000000ff);
    if (*a != 255) {
        *r = mul_div_255_round(*r, *a);
        *g = mul_div_255_round(*g, *a);
        *b = mul_div_255_round(*b, *a);
    }
}

/* width must be a power of two. */
static void blur_rect(uint32_t* dst, const uint32_t* src, uint32_t width,
                      uint32_t height, uint32_t stride, int radius) {
    ZX_ASSERT(fbl::is_pow2(width));
    ZX_ASSERT(radius > 0);

    int width_mask = width - 1;
    int radius0 = -radius;
    int radius1 = radius + 1;
    int size = radius + radius + 1;

    for (uint32_t y = 0; y < height; y++) {
        uint8_t a, r, g, b;
        uint32_t a32 = 0;
        uint32_t r32 = 0;
        uint32_t g32 = 0;
        uint32_t b32 = 0;

        for (int x = radius0; x < radius1; ++x) {
            argb_8888_unpack_mul(src[x & width_mask], &a, &r, &g, &b);
            a32 += a;
            r32 += r;
            g32 += g;
            b32 += b;
        }

        for (int x = 0; x <= width_mask; ++x) {
            dst[x] = a32 ? ((a32 / size) << 24) | (((255 * r32) / a32) << 16) |
                               (((255 * g32) / a32) << 8) |
                               (((255 * b32) / a32))
                         : 0;

            argb_8888_unpack_mul(src[(x + radius0) & width_mask], &a, &r, &g,
                                 &b);
            a32 -= a;
            r32 -= r;
            g32 -= g;
            b32 -= b;

            argb_8888_unpack_mul(src[(x + radius1) & width_mask], &a, &r, &g,
                                 &b);
            a32 += a;
            r32 += r;
            g32 += g;
            b32 += b;
        }

        src += stride;
        dst += stride;
    }
}

static bool is_rect_empty(const rect_t* rect) {
    return rect->x1 >= rect->x2 && rect->y1 >= rect->y2;
}

static void union_rects(rect_t* dst, const rect_t* a, const rect_t* b) {
    if (is_rect_empty(b)) {
        *dst = *a;
    } else if (is_rect_empty(a)) {
        *dst = *b;
    } else {
        dst->x1 = fbl::min(a->x1, b->x1);
        dst->y1 = fbl::min(a->y1, b->y1);
        dst->x2 = fbl::max(a->x2, b->x2);
        dst->y2 = fbl::max(a->y2, b->y2);
    }
}

static fbl::String rect_as_string(const rect_t* rect) {
    return fbl::StringPrintf("%d,%d %dx%d", rect->x1, rect->y1,
                             rect->x2 - rect->x1, rect->y2 - rect->y1);
}

static void prepare_poll(int touchfd, int touchpadfd, int* startfd, int* endfd,
                         struct pollfd* fds) {
    *startfd = 1;
    *endfd = 1;
    if (touchfd >= 0) {
        fds[0].fd = touchfd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        *startfd = 0;
    }
    if (touchpadfd >= 0) {
        fds[1].fd = touchpadfd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        *endfd = 2;
    }
}

static zx_status_t compute_linear_image_stride(uint32_t width,
                                               zx_pixel_format_t format,
                                               uint32_t* stride_out) {
    zx_status_t status;

    fuchsia_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal =
        fuchsia_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.hdr.txid = txid++;
    stride_msg.width = width;
    stride_msg.pixel_format = format;

    fuchsia_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &stride_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        return status;
    }

    *stride_out = stride_rsp.stride;
    return ZX_OK;
}

static zx_status_t import_image(zx_handle_t handle, uint32_t width,
                                uint32_t height, zx_pixel_format_t format,
                                uint64_t* id_out) {
    zx_status_t status;
    zx_handle_t dup;
    status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
    ZX_ASSERT(status == ZX_OK);

    fuchsia_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_display_ControllerImportVmoImageOrdinal;
    import_msg.hdr.txid = txid++;
    import_msg.image_config.height = height;
    import_msg.image_config.width = width;
    import_msg.image_config.pixel_format = format;
    import_msg.image_config.type = IMAGE_TYPE_SIMPLE;
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;

    fuchsia_display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t import_call = {};
    import_call.wr_bytes = &import_msg;
    import_call.wr_handles = &dup;
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

static void release_image(uint64_t image_id) {
    fuchsia_display_ControllerReleaseEventRequest release_img_msg;
    release_img_msg.hdr.ordinal = fuchsia_display_ControllerReleaseEventOrdinal;
    release_img_msg.hdr.txid = txid++;
    release_img_msg.id = image_id;
    zx_channel_write(dc_handle, 0, &release_img_msg, sizeof(release_img_msg),
                     NULL, 0);
}

static zx_status_t import_event(zx_handle_t handle, uint64_t id) {
    zx_status_t status;
    zx_handle_t dup;
    status = zx_handle_duplicate(handle, ZX_RIGHT_SAME_RIGHTS, &dup);
    ZX_ASSERT(status == ZX_OK);

    fuchsia_display_ControllerImportEventRequest import_evt_msg;
    import_evt_msg.hdr.ordinal = fuchsia_display_ControllerImportEventOrdinal;
    import_evt_msg.hdr.txid = txid++;
    import_evt_msg.id = id;
    import_evt_msg.event = FIDL_HANDLE_PRESENT;
    return zx_channel_write(dc_handle, 0, &import_evt_msg,
                            sizeof(import_evt_msg), &dup, 1);
}

static void release_event(uint64_t id) {
    fuchsia_display_ControllerReleaseEventRequest release_evt_msg;
    release_evt_msg.hdr.ordinal = fuchsia_display_ControllerReleaseEventOrdinal;
    release_evt_msg.hdr.txid = txid++;
    release_evt_msg.id = id;
    zx_channel_write(dc_handle, 0, &release_evt_msg, sizeof(release_evt_msg),
                     NULL, 0);
}

static zx_status_t create_layer(uint64_t* layer_id_out) {
    zx_status_t status;

    fuchsia_display_ControllerCreateLayerRequest create_layer_msg;
    create_layer_msg.hdr.ordinal = fuchsia_display_ControllerCreateLayerOrdinal;

    fuchsia_display_ControllerCreateLayerResponse create_layer_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &create_layer_msg;
    call_args.rd_bytes = &create_layer_rsp;
    call_args.wr_num_bytes = sizeof(create_layer_msg);
    call_args.rd_num_bytes = sizeof(create_layer_rsp);
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        return status;
    }
    if (create_layer_rsp.res != ZX_OK) {
        return create_layer_rsp.res;
    }

    *layer_id_out = create_layer_rsp.layer_id;
    return ZX_OK;
}

static zx_status_t set_display_layers(uint64_t display_id, uint64_t layer_id,
                                      uint64_t sprite_layer_id) {
    uint8_t
        fidl_bytes[sizeof(fuchsia_display_ControllerSetDisplayLayersRequest) +
                   FIDL_ALIGN(sizeof(uint64_t) * 2)];
    fuchsia_display_ControllerSetDisplayLayersRequest* display_layers_msg =
        (fuchsia_display_ControllerSetDisplayLayersRequest*)fidl_bytes;
    display_layers_msg->hdr.ordinal =
        fuchsia_display_ControllerSetDisplayLayersOrdinal;
    display_layers_msg->display_id = display_id;
    display_layers_msg->layer_ids.count = 2;
    display_layers_msg->layer_ids.data = (void*)FIDL_ALLOC_PRESENT;
    uint64_t* layer_list = (uint64_t*)(display_layers_msg + 1);
    layer_list[0] = layer_id;
    layer_list[1] = sprite_layer_id;
    return zx_channel_write(dc_handle, 0, fidl_bytes, sizeof(fidl_bytes), NULL,
                            0);
}

static zx_status_t set_layer_config(uint64_t layer_id, uint32_t width,
                                    uint32_t height, zx_pixel_format_t format) {
    fuchsia_display_ControllerSetLayerPrimaryConfigRequest layer_cfg_msg;
    layer_cfg_msg.hdr.ordinal =
        fuchsia_display_ControllerSetLayerPrimaryConfigOrdinal;
    layer_cfg_msg.layer_id = layer_id;
    layer_cfg_msg.image_config.width = width;
    layer_cfg_msg.image_config.height = height;
    layer_cfg_msg.image_config.pixel_format = format;
    layer_cfg_msg.image_config.type = IMAGE_TYPE_SIMPLE;
    return zx_channel_write(dc_handle, 0, &layer_cfg_msg, sizeof(layer_cfg_msg),
                            NULL, 0);
}

static zx_status_t set_layer_alpha(uint64_t layer_id, bool alpha) {
    fuchsia_display_ControllerSetLayerPrimaryAlphaRequest alpha_msg;
    alpha_msg.hdr.ordinal =
        fuchsia_display_ControllerSetLayerPrimaryAlphaOrdinal;
    alpha_msg.layer_id = layer_id;
    alpha_msg.mode = alpha ? fuchsia_display_AlphaMode_HW_MULTIPLY
                           : fuchsia_display_AlphaMode_DISABLE;
    alpha_msg.val = 1.0;
    return zx_channel_write(dc_handle, 0, &alpha_msg, sizeof(alpha_msg), NULL,
                            0);
}

static zx_status_t set_layer_position(uint64_t layer_id, uint32_t src_x,
                                      uint32_t src_y, uint32_t dest_x,
                                      uint32_t dest_y, uint32_t width,
                                      uint32_t height) {
    fuchsia_display_ControllerSetLayerPrimaryPositionRequest pos_msg;
    pos_msg.hdr.ordinal =
        fuchsia_display_ControllerSetLayerPrimaryPositionOrdinal;
    pos_msg.layer_id = layer_id;
    pos_msg.transform = fuchsia_display_Transform_IDENTITY;
    pos_msg.src_frame.width = width;
    pos_msg.src_frame.height = height;
    pos_msg.src_frame.x_pos = src_x;
    pos_msg.src_frame.y_pos = src_y;
    pos_msg.dest_frame.width = width;
    pos_msg.dest_frame.height = height;
    pos_msg.dest_frame.x_pos = dest_x;
    pos_msg.dest_frame.y_pos = dest_y;
    return zx_channel_write(dc_handle, 0, &pos_msg, sizeof(pos_msg), NULL, 0);
}

static zx_status_t set_layer_image(uint64_t layer_id, uint64_t image_id,
                                   uint64_t wait_event_id) {
    fuchsia_display_ControllerSetLayerImageRequest set_msg;
    set_msg.hdr.ordinal = fuchsia_display_ControllerSetLayerImageOrdinal;
    set_msg.hdr.txid = txid++;
    set_msg.layer_id = layer_id;
    set_msg.image_id = image_id;
    set_msg.wait_event_id = wait_event_id;
    set_msg.signal_event_id = INVALID_ID;
    return zx_channel_write(dc_handle, 0, &set_msg, sizeof(set_msg), NULL, 0);
}

static zx_status_t check_config() {
    fuchsia_display_ControllerCheckConfigRequest check_msg;
    uint8_t check_resp_bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    check_msg.discard = false;
    check_msg.hdr.ordinal = fuchsia_display_ControllerCheckConfigOrdinal;
    zx_channel_call_args_t check_call = {};
    check_call.wr_bytes = &check_msg;
    check_call.rd_bytes = check_resp_bytes;
    check_call.wr_num_bytes = sizeof(check_msg);
    check_call.rd_num_bytes = sizeof(check_resp_bytes);
    uint32_t actual_bytes, actual_handles;
    zx_status_t status;
    if ((status = zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &check_call,
                                  &actual_bytes, &actual_handles)) != ZX_OK) {
        return status;
    }

    const char* err_msg;
    if ((status = fidl_decode(
             &fuchsia_display_ControllerCheckConfigResponseTable,
             check_resp_bytes, actual_bytes, NULL, 0, &err_msg)) != ZX_OK) {
        return ZX_ERR_STOP;
    }

    fuchsia_display_ControllerCheckConfigResponse* check_rsp =
        (fuchsia_display_ControllerCheckConfigResponse*)check_resp_bytes;
    if (check_rsp->res != fuchsia_display_ConfigResult_OK) {
        fprintf(stderr, "config not valid (%d)\n", check_rsp->res);
        auto* arr = static_cast<fuchsia_display_ClientCompositionOp*>(
            check_rsp->ops.data);
        for (unsigned i = 0; i < check_rsp->ops.count; i++) {
            fprintf(stderr,
                    "client composition op (display %ld, layer %ld): %d\n",
                    arr[i].display_id, arr[i].layer_id, arr[i].opcode);
        }
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t apply_config() {
    fuchsia_display_ControllerApplyConfigRequest apply_msg;
    apply_msg.hdr.txid = txid++;
    apply_msg.hdr.ordinal = fuchsia_display_ControllerApplyConfigOrdinal;
    return zx_channel_write(dc_handle, 0, &apply_msg, sizeof(apply_msg), NULL,
                            0);
}

static zx_status_t alloc_image_buffer(uint32_t size, zx_handle_t* vmo_out) {
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

zx_status_t enable_vsync(bool enable) {
    fuchsia_display_ControllerEnableVsyncRequest enable_vsync;
    enable_vsync.hdr.ordinal = fuchsia_display_ControllerEnableVsyncOrdinal;
    enable_vsync.enable = enable;
    return zx_channel_write(dc_handle, 0, &enable_vsync, sizeof(enable_vsync),
                            NULL, 0);
}

zx_status_t wait_for_vsync(zx_time_t* timestamp, uint64_t image_ids[2]) {
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
    if ((status = zx_channel_read(dc_handle, 0, bytes, NULL,
                                  ZX_CHANNEL_MAX_MSG_BYTES, 0, &actual_bytes,
                                  &actual_handles)) != ZX_OK) {
        return ZX_ERR_STOP;
    }

    if (actual_bytes < sizeof(fidl_message_header_t)) {
        return ZX_ERR_INTERNAL;
    }

    fidl_message_header_t* header = (fidl_message_header_t*)bytes;

    switch (header->ordinal) {
    case fuchsia_display_ControllerDisplaysChangedOrdinal:
        return ZX_ERR_STOP;
    case fuchsia_display_ControllerClientOwnershipChangeOrdinal:
        return ZX_ERR_NEXT;
    case fuchsia_display_ControllerVsyncOrdinal:
        break;
    default:
        return ZX_ERR_STOP;
    }

    const char* err_msg;
    if ((status = fidl_decode(&fuchsia_display_ControllerVsyncEventTable, bytes,
                              actual_bytes, NULL, 0, &err_msg)) != ZX_OK) {
        return ZX_ERR_STOP;
    }

    fuchsia_display_ControllerVsyncEvent* vsync =
        (fuchsia_display_ControllerVsyncEvent*)bytes;
    *timestamp = vsync->timestamp;
    image_ids[0] = vsync->images.count > 0 ? ((uint64_t*)vsync->images.data)[0]
                                           : INVALID_ID;
    image_ids[1] = vsync->images.count > 1 ? ((uint64_t*)vsync->images.data)[1]
                                           : INVALID_ID;
    return ZX_OK;
}

static void print_usage(FILE* stream) {
    fprintf(
        stream,
        "usage: gfxlatency [options]\n\n"
        "options:\n"
        "  -h, --help\t\t\tPrint this help\n"
        "  --vsync=on|off|adaptive\tVSync mode (default=adaptive)\n"
        "  --offset=MS\t\t\tVSync offset (default=15)\n"
        "  --pen-prediction=MS\t\tPen prediction (default=15)\n"
        "  --scroll-prediction=MS\tScroll prediction (default=15)\n"
        "  --prediction-color=COLOR\tPrediction color (default=0x7f000000)\n"
        "  --slow-down-scale-factor=NUM\tUpdate each line multiple times "
        "(default=1)\n");
}

int main(int argc, char* argv[]) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    trace::TraceProvider provider(loop.dispatcher());

    VSync vsync = VSync::ADAPTIVE;
    zx_time_t vsync_offset = ZX_MSEC(15);
    int slow_down_scale_factor = 1;
    uint32_t pen_prediction_ms = 15;
    uint32_t scroll_prediction_ms = 15;
    uint32_t prediction_color = 0x7f000000;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strstr(arg, "--vsync") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            if (!strcmp(s, "on")) {
                vsync = VSync::ON;
            } else if (!strcmp(s, "off")) {
                vsync = VSync::OFF;
            } else if (!strcmp(s, "adaptive")) {
                vsync = VSync::ADAPTIVE;
            } else {
                fprintf(stderr, "invalid vsync mode: %s\n", s);
                print_usage(stderr);
                return -1;
            }
        } else if (strstr(arg, "--offset") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            vsync_offset = ZX_MSEC(atoi(s));
        } else if (strstr(arg, "--pen-prediction") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            pen_prediction_ms = atoi(s);
        } else if (strstr(arg, "--prediction-color") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            prediction_color = (uint32_t)strtol(s, NULL, 16);
        } else if (strstr(arg, "--scroll-prediction") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            scroll_prediction_ms = atoi(s);
        } else if (strstr(arg, "--slow-down-scale-factor") == arg) {
            const char* s = strchr(arg, '=');
            ++s;
            slow_down_scale_factor = fbl::max(1, atoi(s));
        } else if (strstr(arg, "-h") == arg) {
            print_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "invalid argument: %s\n", arg);
            print_usage(stderr);
            return -1;
        }
    }

    int32_t dc_fd = open("/dev/class/display-controller/000", O_RDWR);
    if (dc_fd < 0) {
        fprintf(stderr, "failed to open display controller\n");
        return -1;
    }

    if (ioctl_display_controller_get_handle(dc_fd, &dc_handle) !=
        sizeof(zx_handle_t)) {
        fprintf(stderr, "failed to get display controller handle\n");
        return -1;
    }

    zx_status_t status;
    zx_handle_t observed;
    uint32_t signals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;
    if ((status = zx_object_wait_one(dc_handle, signals, ZX_TIME_INFINITE,
                                     &observed)) != ZX_OK) {
        fprintf(stderr, "failed waiting for display\n");
        return -1;
    }
    if (observed & ZX_CHANNEL_PEER_CLOSED) {
        fprintf(stderr, "display controller connection closed\n");
        return -1;
    }

    uint8_t bytes[ZX_CHANNEL_MAX_MSG_BYTES];
    uint32_t actual_bytes, actual_handles;
    if ((status = zx_channel_read(dc_handle, 0, bytes, NULL,
                                  ZX_CHANNEL_MAX_MSG_BYTES, 0, &actual_bytes,
                                  &actual_handles)) != ZX_OK) {
        fprintf(stderr, "reading display addded callback failed\n");
        return -1;
    }

    const char* err_msg;
    if ((status =
             fidl_decode(&fuchsia_display_ControllerDisplaysChangedEventTable,
                         bytes, actual_bytes, NULL, 0, &err_msg)) != ZX_OK) {
        fprintf(stderr, "%s\n", err_msg);
        return -1;
    }

    // We're guaranteed that added contains at least one display, since we
    // haven't been notified of any displays to remove.
    fuchsia_display_ControllerDisplaysChangedEvent* changes =
        (fuchsia_display_ControllerDisplaysChangedEvent*)bytes;
    fuchsia_display_Info* display = (fuchsia_display_Info*)changes->added.data;
    fuchsia_display_Mode* mode = (fuchsia_display_Mode*)display->modes.data;

    uint32_t width = mode->horizontal_resolution;
    uint32_t height = mode->vertical_resolution;
    zx_pixel_format_t format = ((int32_t*)(display->pixel_format.data))[0];

    uint32_t stride;
    if (compute_linear_image_stride(width, format, &stride) != ZX_OK) {
        fprintf(stderr, "failed to get linear stride\n");
        return -1;
    }

    uint64_t layer_id;
    if (create_layer(&layer_id) != ZX_OK) {
        fprintf(stderr, "failed to create layer\n");
        return -1;
    }

    uint32_t sprite_stride;
    if (compute_linear_image_stride(SPRITE_DIM, SPRITE_FORMAT,
                                    &sprite_stride) != ZX_OK) {
        fprintf(stderr, "failed to get linear stride\n");
        return -1;
    }

    uint64_t sprite_layer_id;
    if (create_layer(&sprite_layer_id) != ZX_OK) {
        fprintf(stderr, "failed to create sprite layer\n");
        return -1;
    }

    if (set_display_layers(display->id, layer_id, sprite_layer_id) != ZX_OK) {
        fprintf(stderr, "failed to set display layers\n");
        return -1;
    }

    if ((status = set_layer_config(layer_id, width, height, format)) != ZX_OK) {
        fprintf(stderr, "failed to set layer config\n");
        return -1;
    }

    if ((status = set_layer_config(sprite_layer_id, SPRITE_DIM, SPRITE_DIM,
                                   format)) != ZX_OK) {
        fprintf(stderr, "failed to set sprite layer config\n");
        return -1;
    }

    uint32_t buffer_size = ZX_PIXEL_FORMAT_BYTES(format) * height * stride;
    uint32_t canvas_width = width * 2;
    uint32_t canvas_height = height * 2;
    fbl::unique_ptr<uint8_t[]> surface_data(
        new uint8_t[ZX_PIXEL_FORMAT_BYTES(format) * canvas_width *
                    canvas_height]);
    gfx_surface* surface =
        gfx_create_surface((void*)surface_data.get(), canvas_width,
                           canvas_height, canvas_width, format, 0);
    ZX_ASSERT(surface);
    {
        TRACE_DURATION("app", "Initialize Canvas");

        // Initialize using background image if format allows.
        if (ZX_PIXEL_FORMAT_BYTES(format) == 4) {
            copy_rect((uint32_t*)surface->ptr,
                      (const uint32_t*)background_image.pixel_data,
                      canvas_width, background_image.width, 0, 0,
                      background_image.width, background_image.height);
            for (uint32_t y = 0; y < canvas_height;
                 y += background_image.height) {
                for (uint32_t x = 0; x < canvas_width;
                     x += background_image.width) {
                    gfx_copyrect(surface, 0, 0, background_image.width,
                                 background_image.height, x, y);
                }
            }
        } else {
            gfx_clear(surface, 0xffffffff);
        }
    }

    fbl::unique_ptr<uint8_t[]> sprite_surface_data(
        new uint8_t[ZX_PIXEL_FORMAT_BYTES(SPRITE_FORMAT) * SPRITE_DIM *
                    SPRITE_DIM]);
    gfx_surface* sprite_surface =
        gfx_create_surface((void*)sprite_surface_data.get(), SPRITE_DIM,
                           SPRITE_DIM, SPRITE_DIM, SPRITE_FORMAT, 0);
    ZX_ASSERT(sprite_surface);
    gfx_clear(sprite_surface, 0);

    // Scratch buffer for sprite updates. 2 times the size of the sprite.
    fbl::unique_ptr<uint8_t[]> sprite_scratch(
        new uint8_t[ZX_PIXEL_FORMAT_BYTES(SPRITE_FORMAT) * SPRITE_DIM *
                    SPRITE_DIM * 2]);
    memset(sprite_scratch.get(), 0,
           ZX_PIXEL_FORMAT_BYTES(SPRITE_FORMAT) * SPRITE_DIM * SPRITE_DIM * 2);

    pointf_t pen[NUM_PENCILS] = {{.x = NAN, .y = NAN}, {.x = NAN, .y = NAN}};
    point_t sprite_location = {.x = width / 2, .y = height / 2};
    vector_t sprite_hotspot = {.x = SPRITE_RAD, .y = SPRITE_RAD};
    pointf_t cursor = {.x = (float)sprite_location.x,
                       .y = (float)sprite_location.y};
    pointf_t touch[NUM_FINGERS] = {{.x = NAN, .y = NAN},
                                   {.x = NAN, .y = NAN},
                                   {.x = NAN, .y = NAN},
                                   {.x = NAN, .y = NAN},
                                   {.x = NAN, .y = NAN}};
    point_t origin = {.x = canvas_width / 2 - width / 2,
                      .y = canvas_height / 2 - height / 2};
    point_t predicted_origin = origin;
    vectorf_t origin_delta = {.x = 0, .y = 0};

    uint64_t next_event_id = INVALID_ID + 1;

    buffer_t buffer_storage[NUM_BUFFERS];
    fbl::Array<buffer_t> buffers(buffer_storage,
                                 vsync == VSync::OFF ? 1 : NUM_BUFFERS);
    for (auto& buffer : buffers) {
        status = alloc_image_buffer(buffer_size, &buffer.vmo);
        ZX_ASSERT(status == ZX_OK);

        zx_vmo_set_cache_policy(buffer.vmo, ZX_CACHE_POLICY_WRITE_COMBINING);

        status =
            import_image(buffer.vmo, width, height, format, &buffer.image_id);
        ZX_ASSERT(status == ZX_OK);

        status = zx_event_create(0, &buffer.wait_event);
        ZX_ASSERT(status == ZX_OK);
        buffer.wait_event_id = INVALID_ID;
        if (vsync == VSync::ON) {
            buffer.wait_event_id = next_event_id++;
            status = import_event(buffer.wait_event, buffer.wait_event_id);
            ZX_ASSERT(status == ZX_OK);
        }
        zx_object_signal(buffer.wait_event, 0, ZX_EVENT_SIGNALED);

        status = zx_vmar_map(zx_vmar_root_self(), 0, buffer.vmo, 0, buffer_size,
                             ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                             &buffer.data);
        ZX_ASSERT(status == ZX_OK);
        copy_rect((uint32_t*)buffer.data,
                  (const uint32_t*)surface->ptr + origin.y * canvas_width +
                      origin.x,
                  stride, canvas_width, 0, 0, width, height);
        memset(&buffer.damage, 0, sizeof(buffer.damage));
    }

    uint32_t sprite_size =
        ZX_PIXEL_FORMAT_BYTES(format) * SPRITE_DIM * sprite_stride;
    buffer_t sprite_storage[NUM_BUFFERS];
    fbl::Array<buffer_t> sprites(sprite_storage,
                                 vsync == VSync::OFF ? 1 : NUM_BUFFERS);
    for (auto& sprite : sprites) {
        status = alloc_image_buffer(sprite_size, &sprite.vmo);
        ZX_ASSERT(status == ZX_OK);

        zx_vmo_set_cache_policy(sprite.vmo, ZX_CACHE_POLICY_WRITE_COMBINING);

        status = import_image(sprite.vmo, SPRITE_DIM, SPRITE_DIM, SPRITE_FORMAT,
                              &sprite.image_id);
        ZX_ASSERT(status == ZX_OK);

        status = zx_event_create(0, &sprite.wait_event);
        ZX_ASSERT(status == ZX_OK);
        sprite.wait_event_id = INVALID_ID;
        if (vsync == VSync::ON) {
            sprite.wait_event_id = next_event_id++;
            status = import_event(sprite.wait_event, sprite.wait_event_id);
            ZX_ASSERT(status == ZX_OK);
        }
        zx_object_signal(sprite.wait_event, 0, ZX_EVENT_SIGNALED);

        status = zx_vmar_map(zx_vmar_root_self(), 0, sprite.vmo, 0, sprite_size,
                             ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                             &sprite.data);
        ZX_ASSERT(status == ZX_OK);
        memset((void*)sprite.data, 0, sprite_size);
        memset(&sprite.damage, 0, sizeof(sprite.damage));
    }

    // Enable vsync if needed.
    if (vsync != VSync::OFF) {
        if ((status = enable_vsync(true)) != ZX_OK) {
            fprintf(stderr, "failed to enable vsync\n");
            return -1;
        }
        TRACE_ASYNC_BEGIN("app", "Buffer Scheduled", (uintptr_t)&buffers[0],
                          "image", buffers[0].image_id);
        TRACE_ASYNC_BEGIN("app", "Sprite Scheduled", (uintptr_t)&sprites[0],
                          "image", sprites[0].image_id);
    }

    // Set initial image for root layer.
    if ((status = set_layer_image(layer_id, buffers[0].image_id, INVALID_ID)) !=
        ZX_OK) {
        fprintf(stderr, "failed to set layer image\n");
        return -1;
    }
    // Set initial image and position for sprite layer.
    if ((status = set_layer_image(sprite_layer_id, sprites[0].image_id,
                                  INVALID_ID)) != ZX_OK) {
        fprintf(stderr, "failed to set sprite layer image\n");
        return -1;
    }
    if ((status = set_layer_position(sprite_layer_id, 0, 0,
                                     sprite_location.x - sprite_hotspot.x,
                                     sprite_location.y - sprite_hotspot.y,
                                     SPRITE_DIM, SPRITE_DIM)) != ZX_OK) {
        fprintf(stderr, "failed to set sprite layer position\n");
        return -1;
    }
    if ((status = set_layer_alpha(sprite_layer_id, true)) != ZX_OK) {
        fprintf(stderr, "failed to set sprite layer alpha\n");
        return -1;
    }

    // Check initial layer config. We assume that movement to the sprite layer
    // doesn't require another check.
    if ((status = check_config()) != ZX_OK) {
        fprintf(stderr, "layer config failed\n");
        return -1;
    }

    // Present initial buffers.
    if ((status = apply_config()) != ZX_OK) {
        fprintf(stderr, "failed to present layers\n");
        return -1;
    }

    DIR* dir = opendir(DEV_INPUT);
    if (!dir) {
        fprintf(stderr, "failed to open %s: %d\n", DEV_INPUT, errno);
        return -1;
    }

    ssize_t ret;
    int touchfd = -1;
    int touchpadfd = -1;
    struct dirent* de;
    while ((de = readdir(dir))) {
        char devname[128];

        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        snprintf(devname, sizeof(devname), "%s/%s", DEV_INPUT, de->d_name);
        int fd = open(devname, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "failed to open %s: %d\n", devname, errno);
            continue;
        }

        size_t rpt_desc_len = 0;
        ret = ioctl_input_get_report_desc_size(fd, &rpt_desc_len);
        if (ret < 0) {
            fprintf(stderr,
                    "failed to get report descriptor length for %s: %zd\n",
                    devname, ret);
            close(fd);
            continue;
        }

        uint8_t rpt_desc[rpt_desc_len];
        ret = ioctl_input_get_report_desc(fd, rpt_desc, rpt_desc_len);
        if (ret < 0) {
            fprintf(stderr, "failed to get report descriptor for %s: %zd\n",
                    devname, ret);
            close(fd);
            continue;
        }

        if (is_paradise_touch_v3_report_desc(rpt_desc, rpt_desc_len)) {
            touchfd = fd;
            continue;
        }

        if (is_paradise_touchpad_v2_report_desc(rpt_desc, rpt_desc_len)) {
            touchpadfd = fd;
            continue;
        }

        close(fd);
    }
    closedir(dir);

    if (touchfd < 0 && touchpadfd < 0) {
        fprintf(stderr, "could not find a touch device\n");
        return -1;
    }

    input_report_size_t max_touch_rpt_sz = 0;
    if (touchfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchfd, &max_touch_rpt_sz);
        ZX_ASSERT(ret >= 0);
    }
    input_report_size_t max_touchpad_rpt_sz = 0;
    if (touchpadfd >= 0) {
        ret = ioctl_input_get_max_reportsize(touchpadfd, &max_touchpad_rpt_sz);
        ZX_ASSERT(ret >= 0);
    }

    async::Loop update_loop(&kAsyncLoopConfigNoAttachToThread);
    update_loop.StartThread();
    async::Loop sprite_update_loop(&kAsyncLoopConfigNoAttachToThread);
    sprite_update_loop.StartThread();

    size_t buffer_frame = 0;
    size_t sprite_frame = 0;
    bool buffer_frame_scheduled = vsync != VSync::OFF;
    bool sprite_frame_scheduled = vsync != VSync::OFF;
    bool buffer_update_pending = false;
    bool sprite_update_pending = false;
    bool show_cursor = false;
    fbl::Vector<pointf_t> points[NUM_PENCILS];
    fbl::Vector<line_t> lines;

    // Input prediction state.
    zx_time_t last_input_prediction_update = zx_clock_get_monotonic();
    pointf_t touch0[NUM_FINGERS];
    memcpy(touch0, touch, sizeof(touch));
    pointf_t pen0[NUM_PENCILS];
    memcpy(pen0, pen, sizeof(pen));
    pointf_t origin0 = {.x = (float)origin.x, .y = (float)origin.y};
    pointf_t cursor0 = cursor;
    double cursor_blur_radius = 0;
    double cursor_movement_angle = 0;
    vector_t cursor_blur_offset = {.x = 0, .y = 0};
    vectorf_t origin_responsive_velocity = {.x = 0, .y = 0};
    vectorf_t origin_smooth_velocity = {.x = 0, .y = 0};
    vectorf_t predicted_origin_movement = {.x = 0, .y = 0};
    vectorf_t pen_responsive_velocity[NUM_PENCILS] = {
        {.x = 0, .y = 0},
        {.x = 0, .y = 0},
    };
    vectorf_t pen_smooth_velocity[NUM_PENCILS] = {
        {.x = 0, .y = 0},
        {.x = 0, .y = 0},
    };
    vectorf_t predicted_stylus_movement = {.x = 0, .y = 0};

    async::TaskClosure frame_task([&] {
        if (vsync == VSync::OFF) {
            int startfd, endfd;
            struct pollfd fds[2];

            // Wait for input until it is time to update the input prediction
            // model.
            int timeout = fbl::max((int)(ZX_MSEC(last_input_prediction_update) +
                                         INPUT_PREDICTION_UPDATE_INTERVAL_MS -
                                         ZX_MSEC(zx_clock_get_monotonic())),
                                   0);
            prepare_poll(touchfd, touchpadfd, &startfd, &endfd, fds);
            poll(&fds[startfd], endfd - startfd, timeout);
        } else {
            zx_time_t vsync_time;
            zx_status_t status;

            // Wait for VSync.
            uint64_t image_ids[2] = {INVALID_ID, INVALID_ID};
            while ((status = wait_for_vsync(&vsync_time, image_ids)) != ZX_OK) {
                if (status == ZX_ERR_STOP) {
                    loop.Quit();
                    return;
                }
            }

            // Detect when image from current frame is being scanned out.
            auto& buffer = buffers[buffer_frame % buffers.size()];
            if (buffer_frame_scheduled && (image_ids[0] == buffer.image_id ||
                                           image_ids[1] == buffer.image_id)) {
                TRACE_ASYNC_END("app", "Buffer Scheduled", (uintptr_t)&buffer,
                                "image", buffer.image_id);
                if (buffer_frame > 0) {
                    auto& last_buffer =
                        buffers[(buffer_frame - 1) % buffers.size()];
                    TRACE_ASYNC_END("app", "Buffer Displayed",
                                    (uintptr_t)&last_buffer, "image",
                                    last_buffer.image_id);
                }
                TRACE_ASYNC_BEGIN("app", "Buffer Displayed", (uintptr_t)&buffer,
                                  "image", buffer.image_id);
                buffer_frame_scheduled = false;
            }
            auto& sprite = sprites[sprite_frame % sprites.size()];
            if (sprite_frame_scheduled && (image_ids[0] == sprite.image_id ||
                                           image_ids[1] == sprite.image_id)) {
                TRACE_ASYNC_END("app", "Sprite Scheduled", (uintptr_t)&sprite,
                                "image", sprite.image_id);
                if (sprite_frame > 0) {
                    auto& last_sprite =
                        sprites[(sprite_frame - 1) % sprites.size()];
                    TRACE_ASYNC_END("app", "Sprite Displayed",
                                    (uintptr_t)&last_sprite, "image",
                                    last_sprite.image_id);
                }
                TRACE_ASYNC_BEGIN("app", "Sprite Displayed", (uintptr_t)&sprite,
                                  "image", sprite.image_id);
                sprite_frame_scheduled = false;
            }

            {
                TRACE_DURATION("app", "Waiting For VSync Offset");

                // Wait until vsync + offset.
                zx_nanosleep(vsync_time + vsync_offset);
            }
        }

        // Save current state.
        point_t old_origin = origin;
        pointf_t old_pen[NUM_PENCILS];
        memcpy(old_pen, pen, sizeof(pen));
        point_t old_sprite_location = sprite_location;
        bool old_show_cursor = show_cursor;
        double old_cursor_blur_radius = cursor_blur_radius;
        vector_t old_cursor_blur_offset = cursor_blur_offset;

        // Process all pending input events.
        int ready = 0;
        while (true) {
            int startfd, endfd;
            struct pollfd fds[2];

            prepare_poll(touchfd, touchpadfd, &startfd, &endfd, fds);
            ready = poll(&fds[startfd], endfd - startfd, 0);
            if (!ready)
                break;

            TRACE_DURATION("app", "Process Input Event");

            if (touchfd >= 0 && fds[0].revents) {
                uint8_t rpt_buf[max_touch_rpt_sz];
                ssize_t bytes = read(touchfd, rpt_buf, max_touch_rpt_sz);
                ZX_ASSERT(bytes > 0);

                uint8_t id = *(uint8_t*)rpt_buf;
                if (id == PARADISE_RPT_ID_TOUCH) {
                    paradise_touch_t* rpt = (paradise_touch_t*)rpt_buf;

                    for (uint8_t c = 0; c < NUM_FINGERS; c++) {
                        touch[c].x = NAN;
                        touch[c].y = NAN;
                        if (paradise_finger_flags_tswitch(
                                rpt->fingers[c].flags)) {
                            touch[c].x = (float)scale(rpt->fingers[c].x, width,
                                                      PARADISE_X_MAX);
                            touch[c].y = (float)scale(rpt->fingers[c].y, height,
                                                      PARADISE_Y_MAX);
                            show_cursor = false;
                        }
                    }
                } else if (id == PARADISE_RPT_ID_STYLUS) {
                    paradise_stylus_t* rpt = (paradise_stylus_t*)rpt_buf;

                    if (paradise_stylus_status_tswitch(rpt->status)) {
                        pen[STYLUS_PEN].x =
                            (float)scale(rpt->x, width, PARADISE_STYLUS_X_MAX);
                        pen[STYLUS_PEN].y =
                            (float)scale(rpt->y, height, PARADISE_STYLUS_Y_MAX);
                        points[STYLUS_PEN].push_back(pen[STYLUS_PEN]);
                        show_cursor = false;
                    } else {
                        pen[STYLUS_PEN].x = NAN;
                        pen[STYLUS_PEN].y = NAN;
                    }
                }
            }

            if (touchpadfd >= 0 && fds[1].revents) {
                uint8_t rpt_buf[max_touchpad_rpt_sz];
                ssize_t bytes = read(touchpadfd, rpt_buf, max_touchpad_rpt_sz);
                ZX_ASSERT(bytes > 0);

                paradise_touchpad_t* rpt = (paradise_touchpad_t*)rpt_buf;
                uint32_t contact_count = 0;
                for (uint8_t c = 0; c < NUM_FINGERS; c++) {
                    touch[c].x = NAN;
                    touch[c].y = NAN;
                    if (rpt->fingers[c].tip_switch) {
                        touch[c].x = (float)scale(rpt->fingers[c].x, width,
                                                  PARADISE_X_MAX);
                        touch[c].y = (float)scale(rpt->fingers[c].y, height,
                                                  PARADISE_Y_MAX);
                        ++contact_count;
                    }
                }

                pen[TOUCH_PEN].x = NAN;
                pen[TOUCH_PEN].y = NAN;
                // Show cursor if we only have one contact point.
                if (contact_count == 1 && rpt->fingers[0].tip_switch) {
                    show_cursor = true;
                    if (rpt->button) {
                        pen[TOUCH_PEN].x = cursor.x;
                        pen[TOUCH_PEN].y = cursor.y;
                        points[TOUCH_PEN].push_back(pen[TOUCH_PEN]);
                    }
                }
            }
        }

        // Calculate origin delta from the average touch delta.
        vectorf_t touch_delta = {.x = 0, .y = 0};
        int32_t contact_count = 0;
        for (uint8_t c = 0; c < NUM_FINGERS; c++) {
            if (isnan(touch0[c].x) || isnan(touch[c].x) || isnan(touch0[c].y) ||
                isnan(touch[c].y)) {
                continue;
            }

            // Ignore cursor.
            if (show_cursor && c == 0) {
                continue;
            }

            touch_delta.x += touch[c].x - touch0[c].x;
            touch_delta.y += touch[c].y - touch0[c].y;
            ++contact_count;
        }
        if (contact_count) {
            origin_delta.x = -touch_delta.x / (float)contact_count;
            origin_delta.y = -touch_delta.y / (float)contact_count;
        }

        // Calculate cursor delta. Cursor should only move when no other
        // touch points are active.
        vectorf_t cursor_delta = {.x = 0, .y = 0};
        if (show_cursor && !contact_count && !isnan(touch0[0].x) &&
            !isnan(touch[0].x) && !isnan(touch0[0].y) && !isnan(touch[0].y)) {
            cursor_delta.x = touch[0].x - touch0[0].x;
            cursor_delta.y = touch[0].y - touch0[0].y;
        }

        // Update input prodiction model if enough time has passed. The input
        // prediction model is effected by velocity. Velocity needs to be
        // sampled at an interval to provide a meaningful value.
        zx_time_t current_time = zx_clock_get_monotonic();
        if (ZX_MSEC(current_time - last_input_prediction_update) >=
            INPUT_PREDICTION_UPDATE_INTERVAL_MS) {
            zx_time_t elapsed = current_time - last_input_prediction_update;
            float elapsed_ms = (float)elapsed / ZX_MSEC(1);
            last_input_prediction_update = current_time;

            TRACE_DURATION("app", "Update Input Prediction", "elapsed",
                           elapsed_ms);

            // Update origin prediction.
            pointf_t new_origin = {
                .x = fbl::clamp(origin0.x + origin_delta.x, 0.0f,
                                (float)(surface->width - width)),
                .y = fbl::clamp(origin0.y + origin_delta.y, 0.0f,
                                (float)(surface->height - height))};
            vectorf_t velocity = {
                .x = fbl::clamp((new_origin.x - origin0.x) / elapsed_ms,
                                -ORIGIN_VELOCITY_MAX, ORIGIN_VELOCITY_MAX),
                .y = fbl::clamp((new_origin.y - origin0.y) / elapsed_ms,
                                -ORIGIN_VELOCITY_MAX, ORIGIN_VELOCITY_MAX)};
            // Slowly reduce velocity when we don't have any active touch
            // points.
            if (!contact_count) {
                velocity.x *= 0.95f;
                velocity.y *= 0.95f;
            }
            vector_interpolate(&origin_responsive_velocity,
                               &origin_responsive_velocity, &velocity,
                               RESPONSIVE_VELOCITY_FACTOR);
            vector_interpolate(&origin_smooth_velocity, &origin_smooth_velocity,
                               &velocity, SMOOTH_VELOCITY_FACTOR);

            origin0 = new_origin;
            // Update origin delta to match current touch points when we have
            // active contact points.
            if (contact_count) {
                origin_delta.x = 0;
                origin_delta.y = 0;
                vector_interpolate(
                    &predicted_origin_movement, &origin_responsive_velocity,
                    &origin_smooth_velocity, ORIGIN_MOVEMENT_FACTOR);
                predicted_origin_movement.x *= (float)scroll_prediction_ms;
                predicted_origin_movement.y *= (float)scroll_prediction_ms;
                TRACE_INSTANT("app", "Scroll Prediction", TRACE_SCOPE_THREAD,
                              "dx", predicted_origin_movement.x, "dy",
                              predicted_origin_movement.y);
            } else {
                // Compute a new delta based on velocity when we don't have
                // any active touch points. This results in some motion
                // being maintained after active touch points are gone.
                vector_interpolate(&origin_delta, &origin_responsive_velocity,
                                   &origin_smooth_velocity,
                                   ORIGIN_MOVEMENT_FACTOR);
                origin_delta.x *= elapsed_ms;
                origin_delta.y *= elapsed_ms;
                origin_delta.x += predicted_origin_movement.x;
                origin_delta.y += predicted_origin_movement.y;
                predicted_origin_movement.x = 0;
                predicted_origin_movement.y = 0;
            }

            // Update cursor prediction.
            pointf_t new_cursor = {.x = fbl::clamp(cursor0.x + cursor_delta.x,
                                                   0.0f, (float)(width - 1)),
                                   .y = fbl::clamp(cursor0.y + cursor_delta.y,
                                                   0.0f, (float)(height - 1))};
            vectorf_t pen_velocity = {
                .x = fbl::clamp((new_cursor.x - cursor0.x) / elapsed_ms,
                                -PEN_VELOCITY_MAX, PEN_VELOCITY_MAX),
                .y = fbl::clamp((new_cursor.y - cursor0.y) / elapsed_ms,
                                -PEN_VELOCITY_MAX, PEN_VELOCITY_MAX)};
            vector_interpolate(&pen_responsive_velocity[TOUCH_PEN],
                               &pen_responsive_velocity[TOUCH_PEN],
                               &pen_velocity, RESPONSIVE_VELOCITY_FACTOR);
            vector_interpolate(&pen_smooth_velocity[TOUCH_PEN],
                               &pen_smooth_velocity[TOUCH_PEN], &pen_velocity,
                               SMOOTH_VELOCITY_FACTOR);
            vectorf_t movement;
            vector_interpolate(&movement, &pen_responsive_velocity[TOUCH_PEN],
                               &pen_smooth_velocity[TOUCH_PEN],
                               PEN_MOVEMENT_FACTOR);
            movement.x *= CURSOR_MOVEMENT_PREDICTION_MS;
            movement.y *= CURSOR_MOVEMENT_PREDICTION_MS;
            TRACE_INSTANT("app", "Cursor Prediction", TRACE_SCOPE_THREAD, "dx",
                          movement.x, "dy", movement.y);

            double distance =
                sqrt(movement.x * movement.x + movement.y * movement.y);
            if (distance >= MIN_MOVEMENT_FOR_CURSOR_MOTION_BLUR) {
                cursor_movement_angle = atan2(movement.y, movement.x);
                cursor_blur_radius =
                    fbl::min(round(distance / 2), MAX_BLUR_RADIUS);
                cursor_blur_offset.x =
                    (int32_t)round(movement.x * cursor_blur_radius / distance);
                cursor_blur_offset.y =
                    (int32_t)round(movement.y * cursor_blur_radius / distance);
            } else {
                cursor_movement_angle = 0;
                cursor_blur_radius = 0;
                cursor_blur_offset.x = 0;
                cursor_blur_offset.y = 0;
            }

            cursor0 = new_cursor;
            cursor_delta.x = 0;
            cursor_delta.y = 0;

            memcpy(touch0, touch, sizeof(touch));

            // Update pen prediction.
            pen_velocity = {.x = 0, .y = 0};
            if (!isnan(pen0[STYLUS_PEN].x) && !isnan(pen[STYLUS_PEN].x) &&
                !isnan(pen0[STYLUS_PEN].y) && !isnan(pen[STYLUS_PEN].y)) {
                pen_velocity = {
                    .x = fbl::clamp((pen[STYLUS_PEN].x - pen0[STYLUS_PEN].x) /
                                        elapsed_ms,
                                    -PEN_VELOCITY_MAX, PEN_VELOCITY_MAX),
                    .y = fbl::clamp((pen[STYLUS_PEN].y - pen0[STYLUS_PEN].y) /
                                        elapsed_ms,
                                    -PEN_VELOCITY_MAX, PEN_VELOCITY_MAX)};
            }
            vector_interpolate(&pen_responsive_velocity[STYLUS_PEN],
                               &pen_responsive_velocity[STYLUS_PEN],
                               &pen_velocity, RESPONSIVE_VELOCITY_FACTOR);
            vector_interpolate(&pen_smooth_velocity[STYLUS_PEN],
                               &pen_smooth_velocity[STYLUS_PEN], &pen_velocity,
                               SMOOTH_VELOCITY_FACTOR);
            vector_interpolate(&predicted_stylus_movement,
                               &pen_responsive_velocity[STYLUS_PEN],
                               &pen_smooth_velocity[STYLUS_PEN],
                               PEN_MOVEMENT_FACTOR);
            predicted_stylus_movement.x *= (float)pen_prediction_ms;
            predicted_stylus_movement.y *= (float)pen_prediction_ms;
            TRACE_INSTANT("app", "Pen Prediction", TRACE_SCOPE_THREAD, "dx",
                          predicted_stylus_movement.x, "dy",
                          predicted_stylus_movement.y);

            pen0[STYLUS_PEN] = pen[STYLUS_PEN];
        }

        // Determine new origin. This might add lines if pencils are active.
        origin.x = fbl::clamp((int32_t)round(origin0.x + origin_delta.x), 0,
                              (int32_t)(surface->width - width));
        origin.y = fbl::clamp((int32_t)round(origin0.y + origin_delta.y), 0,
                              (int32_t)(surface->height - height));
        if (origin.x != old_origin.x || origin.y != old_origin.y) {
            rect_t damage = {.x1 = 0, .y1 = 0, .x2 = width, .y2 = height};
            for (auto& buffer : buffers) {
                union_rects(&buffer.damage, &buffer.damage, &damage);
            }

            // Update lines if penciles were active during change to origin.
            for (size_t i = 0; i < NUM_PENCILS; ++i) {
                if (!isnan(pen[i].x) && !isnan(old_pen[i].x) &&
                    !isnan(pen[i].y) && !isnan(old_pen[i].y)) {
                    lines.push_back(
                        {{(uint32_t)round(old_pen[i].x) + old_origin.x,
                          (uint32_t)round(old_pen[i].y) + old_origin.y},
                         {(uint32_t)round(pen[i].x) + origin.x,
                          (uint32_t)round(pen[i].y) + origin.y}});
                    points[i].reset();
                }
            }
        }

        // Determine new predicted origin.
        predicted_origin.x =
            fbl::clamp((int32_t)round(origin0.x + origin_delta.x +
                                      predicted_origin_movement.x),
                       0, (int32_t)(surface->width - width));
        predicted_origin.y =
            fbl::clamp((int32_t)round(origin0.y + origin_delta.y +
                                      predicted_origin_movement.y),
                       0, (int32_t)(surface->height - height));
        if (predicted_origin.x != origin.x || predicted_origin.y != origin.y) {
            rect_t damage = {.x1 = 0, .y1 = 0, .x2 = width, .y2 = height};
            for (auto& buffer : buffers) {
                union_rects(&buffer.damage, &buffer.damage, &damage);
            }
        }

        // Full sprite damage if cursor or stylus pen state changed.
        if (old_show_cursor != show_cursor ||
            !isnan(pen[STYLUS_PEN].x) != !isnan(old_pen[STYLUS_PEN].x) ||
            !isnan(pen[STYLUS_PEN].y) != !isnan(old_pen[STYLUS_PEN].y)) {
            rect_t damage = {
                .x1 = 0, .y1 = 0, .x2 = SPRITE_DIM, .y2 = SPRITE_DIM};
            for (auto& sprite : sprites) {
                union_rects(&sprite.damage, &sprite.damage, &damage);
            }
        }

        // Determine new cursor position.
        if (show_cursor) {
            cursor.x = cursor0.x + cursor_delta.x;
            cursor.y = cursor0.y + cursor_delta.y;

            sprite_location.x =
                (uint32_t)round(cursor.x - (float)cursor_blur_offset.x);
            sprite_location.y =
                (uint32_t)round(cursor.y - (float)cursor_blur_offset.y);
            sprite_hotspot.x =
                SPRITE_RAD - cursor_image.width / 2 + CURSOR_HOTSPOT_X;
            sprite_hotspot.y =
                SPRITE_RAD - cursor_image.height / 2 + CURSOR_HOTSPOT_Y;

            if (cursor_blur_radius != old_cursor_blur_radius ||
                cursor_blur_offset.x != old_cursor_blur_offset.y ||
                cursor_blur_offset.x != old_cursor_blur_offset.y) {
                // TODO(reveman): Limit damage to area of sprite that changed.
                rect_t damage = {
                    .x1 = 0, .y1 = 0, .x2 = SPRITE_DIM, .y2 = SPRITE_DIM};
                for (auto& sprite : sprites) {
                    union_rects(&sprite.damage, &sprite.damage, &damage);
                }
            }
        }

        // Handle stylus prediction.
        if (pen_prediction_ms && !isnan(pen[STYLUS_PEN].x) &&
            !isnan(pen[STYLUS_PEN].y)) {
            sprite_location.x = (uint32_t)round(pen[STYLUS_PEN].x +
                                                predicted_stylus_movement.x);
            sprite_location.y = (uint32_t)round(pen[STYLUS_PEN].y +
                                                predicted_stylus_movement.y);
            sprite_hotspot.x = SPRITE_RAD;
            sprite_hotspot.y = SPRITE_RAD;

            // New prediction point.
            point_t pp = {(uint32_t)round(pen[STYLUS_PEN].x) + SPRITE_RAD -
                              sprite_location.x,
                          (uint32_t)round(pen[STYLUS_PEN].y) + SPRITE_RAD -
                              sprite_location.y};
            rect_t damage = {.x1 = fbl::min(pp.x, SPRITE_RAD),
                             .y1 = fbl::min(pp.y, SPRITE_RAD),
                             .x2 = fbl::max(pp.x, SPRITE_RAD) + 1,
                             .y2 = fbl::max(pp.y, SPRITE_RAD) + 1};

            // Old prediction point.
            if (!isnan(old_pen[STYLUS_PEN].x) &&
                !isnan(old_pen[STYLUS_PEN].y)) {
                point_t pp = {(uint32_t)round(old_pen[STYLUS_PEN].x) +
                                  SPRITE_RAD - old_sprite_location.x,
                              (uint32_t)round(old_pen[STYLUS_PEN].y) +
                                  SPRITE_RAD - old_sprite_location.y};
                rect_t r = {.x1 = fbl::min(pp.x, SPRITE_RAD),
                            .y1 = fbl::min(pp.y, SPRITE_RAD),
                            .x2 = fbl::max(pp.x, SPRITE_RAD) + 1,
                            .y2 = fbl::max(pp.y, SPRITE_RAD) + 1};
                union_rects(&damage, &damage, &r);
            }
            for (auto& sprite : sprites) {
                union_rects(&sprite.damage, &sprite.damage, &damage);
            }
        }

        // Update lines if we have new points from the pencils.
        for (size_t i = 0; i < NUM_PENCILS; ++i) {
            if (points[i].is_empty())
                continue;

            pointf_t p0 = old_pen[i];

            // Convert point to surface coordinate by adding origin.
            if (!isnan(p0.x))
                p0.x += (float)origin.x;
            if (!isnan(p0.y))
                p0.y += (float)origin.y;

            for (auto& p : points[i]) {
                pointf_t p1 = {.x = p.x + (float)origin.x,
                               .y = p.y + (float)origin.y};

                if (!isnan(p0.x) && !isnan(p0.y)) {
                    uint32_t x1 = (uint32_t)round(p0.x);
                    uint32_t y1 = (uint32_t)round(p0.y);
                    uint32_t x2 = (uint32_t)round(p1.x);
                    uint32_t y2 = (uint32_t)round(p1.y);

                    lines.push_back({{x1, y1}, {x2, y2}});

                    rect_t damage = {.x1 = fbl::min(x1, x2) - origin.x,
                                     .y1 = fbl::min(y1, y2) - origin.y,
                                     .x2 = fbl::max(x1, x2) - origin.x + 1,
                                     .y2 = fbl::max(y1, y2) - origin.y + 1};
                    for (auto& buffer : buffers) {
                        union_rects(&buffer.damage, &buffer.damage, &damage);
                    }
                }
                p0 = p1;
            }

            points[i].reset();
        }

        // Update pending and frame scheduled are the same when VSync is on.
        if (vsync == VSync::ON) {
            buffer_update_pending = buffer_frame_scheduled;
            sprite_update_pending = sprite_frame_scheduled;
        } else {
            // Check if updates have completed. This provides back-pressure when
            // not using VSync.
            if (buffer_update_pending) {
                zx_handle_t observed;
                auto& buffer = buffers[buffer_frame % buffers.size()];
                status = zx_object_wait_one(buffer.wait_event,
                                            ZX_EVENT_SIGNALED, 0, &observed);
                buffer_update_pending = (status == ZX_ERR_TIMED_OUT);
            }
            if (sprite_update_pending) {
                zx_handle_t observed;
                auto& sprite = sprites[sprite_frame % sprites.size()];
                status = zx_object_wait_one(sprite.wait_event,
                                            ZX_EVENT_SIGNALED, 0, &observed);
                sprite_update_pending = (status == ZX_ERR_TIMED_OUT);
            }
        }

        bool update_buffer = false;
        bool update_sprite = false;

        // Delay update if frame is scheduled or update is pending.
        if (!buffer_frame_scheduled && !buffer_update_pending) {
            if ((update_buffer = !is_rect_empty(
                     &buffers[buffer_frame % buffers.size()].damage))) {
                auto& buffer = buffers[++buffer_frame % buffers.size()];

                // Reset wait event.
                zx_object_signal(buffer.wait_event, ZX_EVENT_SIGNALED, 0);

                if (vsync != VSync::OFF) {
                    // Present buffer. wait_event_id is invalid when using
                    // adaptive sync as that allows scanout to start event if we
                    // haven't finished producing the new frame.
                    status = set_layer_image(layer_id, buffer.image_id,
                                             buffer.wait_event_id);
                    ZX_ASSERT(status == ZX_OK);

                    buffer_frame_scheduled = true;
                    TRACE_ASYNC_BEGIN("app", "Buffer Scheduled",
                                      (uintptr_t)&buffer, "image",
                                      buffer.image_id);
                }
            }
        }
        if (!sprite_frame_scheduled && !sprite_update_pending) {
            if ((update_sprite = !is_rect_empty(
                     &sprites[sprite_frame % sprites.size()].damage))) {
                auto& sprite = sprites[++sprite_frame % sprites.size()];

                // Reset wait event.
                zx_object_signal(sprite.wait_event, ZX_EVENT_SIGNALED, 0);

                if (vsync != VSync::OFF) {
                    // Present sprite. wait_event_id is invalid when using
                    // adaptive sync as that allows scanout to start event if we
                    // haven't finished producing the new frame.
                    status = set_layer_image(sprite_layer_id, sprite.image_id,
                                             sprite.wait_event_id);
                    ZX_ASSERT(status == ZX_OK);

                    sprite_frame_scheduled = true;
                    TRACE_ASYNC_BEGIN("app", "Sprite Scheduled",
                                      (uintptr_t)&sprite, "image",
                                      sprite.image_id);
                }
            }
        }

        if (update_sprite) {
            auto& sprite = sprites[sprite_frame % sprites.size()];
            rect_t damage = sprite.damage;
            memset(&sprite.damage, 0, sizeof(sprite.damage));
            sprite_update_pending = true;

            // Schedule update on sprite update thread.
            async::PostTask(
                sprite_update_loop.dispatcher(),
                [&sprite_stride, &sprite_surface, &sprite_scratch,
                 &prediction_color, &sprites, sprite_frame, damage,
                 sprite_location, pen, show_cursor, cursor_blur_radius,
                 cursor_movement_angle] {
                    auto& sprite = sprites[sprite_frame % sprites.size()];

                    TRACE_DURATION("app", "Update Sprite", "image",
                                   sprite.image_id, "damage",
                                   rect_as_string(&sprite.damage));

                    ZX_ASSERT(!is_rect_empty(&damage));
                    ZX_ASSERT(sprite_surface->pixelsize == sizeof(uint32_t));

                    if (show_cursor) {
                        ZX_ASSERT(cursor_image.width <= SPRITE_DIM);
                        ZX_ASSERT(cursor_image.height <= SPRITE_DIM);

                        if (cursor_blur_radius > 0.0) {
                            uint32_t* sprite_scratch1 =
                                (uint32_t*)sprite_scratch.get();
                            uint32_t* sprite_scratch2 =
                                sprite_scratch1 + SPRITE_DIM * sprite_stride;
                            uint32_t blur_offset =
                                (SPRITE_RAD - cursor_image.height / 2) *
                                sprite_stride;

                            {
                                TRACE_DURATION("app", "Rotate Cursor", "angle",
                                               cursor_movement_angle);
                                rotate_rect(
                                    sprite_scratch1 +
                                        (SPRITE_RAD - cursor_image.height / 2) *
                                            sprite_stride +
                                        SPRITE_RAD - cursor_image.width / 2,
                                    (const uint32_t*)cursor_image.pixel_data,
                                    cursor_image.width, cursor_image.height,
                                    sprite_stride, cursor_image.width,
                                    cursor_image.height, cursor_image.width,
                                    cursor_image.width / 2,
                                    cursor_image.height / 2,
                                    cursor_image.width / 2,
                                    cursor_image.height / 2,
                                    cursor_movement_angle);
                            }

                            {
                                TRACE_DURATION("app", "Blur Cursor", "radius",
                                               cursor_blur_radius);
                                blur_rect(sprite_scratch2 + blur_offset,
                                          sprite_scratch1 + blur_offset,
                                          SPRITE_DIM, cursor_image.height,
                                          sprite_stride,
                                          (int)cursor_blur_radius);
                            }

                            {
                                TRACE_DURATION("app", "Rotate Sprite", "angle",
                                               -cursor_movement_angle);
                                rotate_rect(
                                    (uint32_t*)sprite.data, sprite_scratch2,
                                    SPRITE_DIM, SPRITE_DIM, sprite_stride,
                                    SPRITE_DIM, SPRITE_DIM, sprite_stride,
                                    SPRITE_RAD, SPRITE_RAD, SPRITE_RAD,
                                    SPRITE_RAD, -cursor_movement_angle);
                            }
                        } else {
                            {
                                TRACE_DURATION("app", "Clear Sprite");
                                gfx_fillrect(sprite_surface, 0, 0, SPRITE_DIM,
                                             SPRITE_DIM, 0);
                            }

                            {
                                TRACE_DURATION("app", "Copy Cursor To Sprite");
                                copy_rect(
                                    ((uint32_t*)sprite_surface->ptr) +
                                        (SPRITE_RAD - cursor_image.height / 2) *
                                            sprite_stride +
                                        SPRITE_RAD - cursor_image.width / 2,
                                    (const uint32_t*)cursor_image.pixel_data,
                                    sprite_surface->stride, cursor_image.width,
                                    0, 0, cursor_image.width,
                                    cursor_image.height);
                            }

                            {
                                TRACE_DURATION("app", "Copy Sprite To Buffer");
                                copy_rect((uint32_t*)sprite.data,
                                          (const uint32_t*)sprite_surface->ptr,
                                          sprite_stride, sprite_surface->stride,
                                          0, 0, SPRITE_DIM, SPRITE_DIM);
                            }
                        }
                    } else {
                        uint32_t x1 = damage.x1;
                        uint32_t y1 = damage.y1;
                        uint32_t x2 = damage.x2;
                        uint32_t y2 = damage.y2;

                        if (x2 > SPRITE_DIM)
                            x2 = SPRITE_DIM;
                        if (y2 > SPRITE_DIM)
                            y2 = SPRITE_DIM;

                        if (x1 < x2 && y1 < y2) {
                            {
                                TRACE_DURATION("app", "Clear Sprite");
                                gfx_fillrect(sprite_surface, x1, y1, x2 - x1,
                                             y2 - y1, 0);
                            }

                            if (!isnan(pen[STYLUS_PEN].x) &&
                                !isnan(pen[STYLUS_PEN].y)) {
                                TRACE_DURATION(
                                    "app", "Draw Stylus Prediction Line", "dx",
                                    (uint32_t)round(pen[STYLUS_PEN].x) -
                                        sprite_location.x,
                                    "dy",
                                    (uint32_t)round(pen[STYLUS_PEN].y) -
                                        sprite_location.y);
                                gfx_line(sprite_surface,
                                         (uint32_t)round(pen[STYLUS_PEN].x) +
                                             SPRITE_RAD - sprite_location.x,
                                         (uint32_t)round(pen[STYLUS_PEN].y) +
                                             SPRITE_RAD - sprite_location.y,
                                         SPRITE_RAD, SPRITE_RAD,
                                         prediction_color);
                            }

                            {
                                TRACE_DURATION("app", "Copy Sprite To Buffer");
                                copy_rect((uint32_t*)sprite.data,
                                          (const uint32_t*)sprite_surface->ptr,
                                          sprite_stride, sprite_surface->stride,
                                          x1, y1, x2, y2);
                            }
                        }
                    }

                    // Signal wait event to communicate that update has
                    // completed.
                    zx_object_signal(sprite.wait_event, 0, ZX_EVENT_SIGNALED);
                });
        }

        if (update_buffer) {
            auto& buffer = buffers[buffer_frame % buffers.size()];
            rect_t damage = buffer.damage;
            memset(&buffer.damage, 0, sizeof(buffer.damage));
            buffer_update_pending = true;

            // Schedule each line on update thread.
            for (auto& line : lines) {
                async::PostTask(update_loop.dispatcher(), [&surface, line] {
                    TRACE_DURATION("app", "Draw Line");
                    gfx_line(surface, line.p1.x, line.p1.y, line.p2.x,
                             line.p2.y, /*color=*/0);
                });
            }
            lines.reset();

            // Schedule buffer update on update thread.
            async::PostTask(
                update_loop.dispatcher(),
                [&stride, &surface, &slow_down_scale_factor, &width, &height,
                 &buffers, buffer_frame, damage, predicted_origin] {
                    auto& buffer = buffers[buffer_frame % buffers.size()];

                    TRACE_DURATION("app", "Update Buffer", "image",
                                   buffer.image_id, "damage",
                                   rect_as_string(&damage));

                    ZX_ASSERT(!is_rect_empty(&damage));

                    uint32_t x1 = damage.x1;
                    uint32_t y1 = damage.y1;
                    uint32_t x2 = damage.x2;
                    uint32_t y2 = damage.y2;

                    if (x2 > width)
                        x2 = width;
                    if (y2 > height)
                        y2 = height;

                    if (x1 < x2 && y1 < y2) {
                        uint32_t pixelsize = surface->pixelsize;
                        uint32_t lines = y2 - y1;
                        uint32_t bytes_per_line = (x2 - x1) * pixelsize;
                        uint32_t dst_pitch = stride * pixelsize;
                        uint32_t src_pitch = surface->stride * pixelsize;
                        uint8_t* dst = ((uint8_t*)buffer.data) +
                                       (y1 * stride + x1) * pixelsize;
                        const uint8_t* src =
                            ((uint8_t*)surface->ptr) +
                            ((y1 + predicted_origin.y) * surface->stride + x1 +
                             predicted_origin.x) *
                                pixelsize;

                        TRACE_DURATION("app", "Copy Contents To Buffer");

                        while (lines--) {
                            int n = slow_down_scale_factor;
                            while (n--)
                                memcpy(dst, src, bytes_per_line);
                            dst += dst_pitch;
                            src += src_pitch;
                        }
                    }

                    // Signal wait event to communicate that update has
                    // completed.
                    zx_object_signal(buffer.wait_event, 0, ZX_EVENT_SIGNALED);
                });
        }

        // Set sprite position.
        int32_t sprite_x1 = sprite_location.x - sprite_hotspot.x;
        int32_t sprite_y1 = sprite_location.y - sprite_hotspot.y;
        int32_t sprite_x2 = sprite_x1 + SPRITE_DIM;
        int32_t sprite_y2 = sprite_y1 + SPRITE_DIM;
        int32_t clipped_sprite_x1 = fbl::max(sprite_x1, 0);
        int32_t clipped_sprite_y1 = fbl::max(sprite_y1, 0);
        int32_t clipped_sprite_x2 = fbl::min(sprite_x2, (int32_t)width);
        int32_t clipped_sprite_y2 = fbl::min(sprite_y2, (int32_t)height);
        ZX_ASSERT(sprite_x1 <= clipped_sprite_x1);
        ZX_ASSERT(sprite_y1 <= clipped_sprite_y1);
        status = set_layer_position(
            sprite_layer_id, clipped_sprite_x1 - sprite_x1,
            clipped_sprite_y1 - sprite_y1, clipped_sprite_x1, clipped_sprite_y1,
            clipped_sprite_x2 - clipped_sprite_x1,
            clipped_sprite_y2 - clipped_sprite_y1);
        ZX_ASSERT(status == ZX_OK);

        status = apply_config();
        ZX_ASSERT(status == ZX_OK);

        frame_task.Post(loop.dispatcher());
    });
    frame_task.Post(loop.dispatcher());

    loop.Run();

    if (touchfd >= 0)
        close(touchfd);
    if (touchpadfd >= 0)
        close(touchpadfd);
    for (auto& buffer : buffers) {
        release_image(buffer.image_id);
        if (buffer.wait_event_id != INVALID_ID)
            release_event(buffer.wait_event_id);
        if (buffer.wait_event != ZX_HANDLE_INVALID)
            zx_handle_close(buffer.wait_event);
        zx_vmar_unmap(zx_vmar_root_self(), buffer.data, buffer_size);
        zx_handle_close(buffer.vmo);
    }
    for (auto& sprite : sprites) {
        release_image(sprite.image_id);
        if (sprite.wait_event_id != INVALID_ID)
            release_event(sprite.wait_event_id);
        if (sprite.wait_event != ZX_HANDLE_INVALID)
            zx_handle_close(sprite.wait_event);
        zx_vmar_unmap(zx_vmar_root_self(), sprite.data, sprite_size);
        zx_handle_close(sprite.vmo);
    }
    gfx_surface_destroy(surface);
    gfx_surface_destroy(sprite_surface);
    zx_handle_close(dc_handle);
    close(dc_fd);
    return 0;
}
