// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <lib/zx/event.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "fuchsia/display/c/fidl.h"
#include "image.h"
#include "utils.h"

static constexpr uint32_t kRenderPeriod = 120;

Image::Image(uint32_t width, uint32_t height, int32_t stride,
             zx_pixel_format_t format, zx_handle_t vmo, void* buf, uint32_t fg_color)
        : width_(width), height_(height), stride_(stride), format_(format),
          vmo_(vmo), buf_(buf), fg_color_(fg_color) {}

Image* Image::Create(zx_handle_t dc_handle,
                     uint32_t width, uint32_t height, zx_pixel_format_t format, uint32_t fg_color) {
    fuchsia_display_ControllerComputeLinearImageStrideRequest stride_msg;
    stride_msg.hdr.ordinal = fuchsia_display_ControllerComputeLinearImageStrideOrdinal;
    stride_msg.width = width;
    stride_msg.pixel_format = format;

    fuchsia_display_ControllerComputeLinearImageStrideResponse stride_rsp;
    zx_channel_call_args_t stride_call = {};
    stride_call.wr_bytes = &stride_msg;
    stride_call.rd_bytes = &stride_rsp;
    stride_call.wr_num_bytes = sizeof(stride_msg);
    stride_call.rd_num_bytes = sizeof(stride_rsp);
    uint32_t actual_bytes, actual_handles;
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE,
                        &stride_call, &actual_bytes, &actual_handles) != ZX_OK) {
        printf("Failed to make stride call\n");
        return nullptr;
    }

    if (stride_rsp.stride < width) {
        printf("Invalid stride\n");
        return nullptr;
    }

    zx::vmo vmo;
    fuchsia_display_ControllerAllocateVmoRequest alloc_msg;
    alloc_msg.hdr.ordinal = fuchsia_display_ControllerAllocateVmoOrdinal;
#if !USE_INTEL_Y_TILING
    alloc_msg.size = stride_rsp.stride * height * ZX_PIXEL_FORMAT_BYTES(format);
#else
    ZX_ASSERT(ZX_PIXEL_FORMAT_BYTES(format) == TILE_BYTES_PER_PIXEL);
    alloc_msg.size = fbl::round_up(width, TILE_PIXEL_WIDTH) *
            fbl::round_up(height, TILE_PIXEL_HEIGHT) * TILE_BYTES_PER_PIXEL;
#endif

    fuchsia_display_ControllerAllocateVmoResponse alloc_rsp;
    zx_channel_call_args_t call_args = {};
    call_args.wr_bytes = &alloc_msg;
    call_args.rd_bytes = &alloc_rsp;
    call_args.rd_handles = vmo.reset_and_get_address();
    call_args.wr_num_bytes = sizeof(alloc_msg);
    call_args.rd_num_bytes = sizeof(alloc_rsp);
    call_args.rd_num_handles = 1;
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &call_args,
                        &actual_bytes, &actual_handles) != ZX_OK) {
        printf("Vmo alloc call failed\n");
        return nullptr;
    }
    if (alloc_rsp.res != ZX_OK) {
        printf("Failed to alloc vmo %d\n", alloc_rsp.res);
        return nullptr;
    }

    uintptr_t addr;
    uint32_t perms = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
    if (zx::vmar::root_self().map(0, vmo, 0, alloc_msg.size, perms, &addr) != ZX_OK) {
        printf("Failed to map vmar\n");
        return nullptr;
    }

    void* ptr = reinterpret_cast<void*>(addr);
    memset(ptr, 0xff, alloc_msg.size);
    zx_cache_flush(ptr, alloc_msg.size, ZX_CACHE_FLUSH_DATA);

    return new Image(width, height, stride_rsp.stride, format, vmo.release(), ptr, fg_color);
}

#define STRIPE_SIZE 37 // prime to make movement more interesting

void Image::Render(int32_t prev_step, int32_t step_num) {
    uint32_t start, end;
    bool draw_stripe;
    if (step_num < 0) {
        start = 0;
        end = height_;
        draw_stripe = true;
    } else {
        uint32_t prev = interpolate(height_, prev_step, kRenderPeriod);
        uint32_t cur = interpolate(height_, step_num, kRenderPeriod);
        start = fbl::min(cur, prev);
        end = fbl::max(cur, prev);
        draw_stripe = cur > prev;
    }

    for (unsigned y = start; y < end; y++) {
        for (unsigned x = 0; x < width_; x++) {
            int32_t in_stripe = draw_stripe && ((x / STRIPE_SIZE % 2) != (y / STRIPE_SIZE % 2));
            int32_t color = in_stripe ? fg_color_: 0xffffffff;

            uint32_t* ptr = static_cast<uint32_t*>(buf_);
#if !USE_INTEL_Y_TILING
            ptr += (y * stride_) + x;
#else
            // Add the offset to the pixel's tile
            uint32_t width_in_tiles = (width_ + TILE_PIXEL_WIDTH - 1) / TILE_PIXEL_WIDTH;
            uint32_t tile_idx = (y / TILE_PIXEL_HEIGHT) * width_in_tiles  + (x / TILE_PIXEL_WIDTH);
            ptr += (TILE_NUM_PIXELS * tile_idx);
            // Add the offset within the pixel's tile
            uint32_t subtile_column_offset =
                    ((x % TILE_PIXEL_WIDTH) / SUBTILE_COLUMN_WIDTH) * TILE_PIXEL_HEIGHT;
            uint32_t subtile_line_offset =
                    (subtile_column_offset + (y % TILE_PIXEL_HEIGHT)) * SUBTILE_COLUMN_WIDTH;
            ptr += subtile_line_offset + (x % SUBTILE_COLUMN_WIDTH);
#endif
            *ptr = color;
        }
    }

#if !USE_INTEL_Y_TILING
    uint32_t byte_stride = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
    zx_cache_flush(reinterpret_cast<uint8_t*>(buf_) + (byte_stride * start),
                   byte_stride * (end - start), ZX_CACHE_FLUSH_DATA);
#else
    uint8_t* buf = static_cast<uint8_t*>(buf_);
    uint32_t width_in_tiles = (width_ + TILE_PIXEL_WIDTH - 1) / TILE_PIXEL_WIDTH;
    uint32_t y_start_tile = start / TILE_PIXEL_HEIGHT;
    uint32_t y_end_tile = (end + TILE_PIXEL_HEIGHT - 1) / TILE_PIXEL_HEIGHT;
    for (unsigned i = 0; i < width_in_tiles; i++) {
        for (unsigned j = y_start_tile; j < y_end_tile; j++) {
            unsigned offset = (TILE_NUM_BYTES * (j * width_in_tiles + i));
            zx_cache_flush(buf + offset, TILE_NUM_BYTES, ZX_CACHE_FLUSH_DATA);
        }
    }
#endif
}

bool Image::Import(zx_handle_t dc_handle, image_import_t* info_out) {
    for (int i = 0; i < 3; i++) {
        static int event_id = INVALID_ID + 1;
        zx_handle_t e1, e2;
        if (zx_event_create(0, &e1) != ZX_OK
                || zx_handle_duplicate(e1, ZX_RIGHT_SAME_RIGHTS, &e2) != ZX_OK) {
            printf("Failed to create event\n");
            return false;
        }

        fuchsia_display_ControllerImportEventRequest import_evt_msg;
        import_evt_msg.hdr.ordinal = fuchsia_display_ControllerImportEventOrdinal;
        import_evt_msg.id = event_id++;
        import_evt_msg.event = FIDL_HANDLE_PRESENT;

        if (zx_channel_write(dc_handle, 0, &import_evt_msg,
                             sizeof(import_evt_msg), &e2, 1) != ZX_OK) {
            printf("Failed to send import message\n");
            return false;
        }

        if (i != WAIT_EVENT) {
            zx_object_signal(e1, 0, ZX_EVENT_SIGNALED);
        }

        info_out->events[i] = e1;
        info_out->event_ids[i] = import_evt_msg.id;
    }

    fuchsia_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_display_ControllerImportVmoImageOrdinal;
    import_msg.image_config.height = height_;
    import_msg.image_config.width = width_;
    import_msg.image_config.pixel_format = format_;
#if !USE_INTEL_Y_TILING
    import_msg.image_config.type = IMAGE_TYPE_SIMPLE;
#else
    import_msg.image_config.type = 2; // IMAGE_TYPE_Y_LEGACY
#endif
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;
    zx_handle_t vmo_dup;
    if (zx_handle_duplicate(vmo_, ZX_RIGHT_SAME_RIGHTS, &vmo_dup) != ZX_OK) {
        printf("Failed to dup handle\n");
        return false;
    }

    fuchsia_display_ControllerImportVmoImageResponse import_rsp;
    zx_channel_call_args_t import_call = {};
    import_call.wr_bytes = &import_msg;
    import_call.wr_handles = &vmo_dup;
    import_call.rd_bytes = &import_rsp;
    import_call.wr_num_bytes = sizeof(import_msg);
    import_call.wr_num_handles = 1;
    import_call.rd_num_bytes = sizeof(import_rsp);
    uint32_t actual_bytes, actual_handles;
    if (zx_channel_call(dc_handle, 0, ZX_TIME_INFINITE, &import_call,
                        &actual_bytes, &actual_handles) != ZX_OK) {
        printf("Failed to make import call\n");
        return false;
    }

    if (import_rsp.res != ZX_OK) {
        printf("Failed to import vmo\n");
        return false;
    }

    info_out->id = import_rsp.image_id;

    return true;
}
