// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <lib/fdio/unsafe.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits>
#include <stdio.h>
#include <string.h>
#include <zircon/device/display-controller.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include "fuchsia/hardware/display/c/fidl.h"
#include "fuchsia/sysmem/c/fidl.h"
#include "image.h"
#include "utils.h"

static constexpr uint32_t kRenderPeriod = 120;

Image::Image(uint32_t width, uint32_t height, int32_t stride,
             zx_pixel_format_t format, zx_handle_t vmo, void* buf,
             uint32_t fg_color, uint32_t bg_color, bool use_intel_y_tiling)
        : width_(width), height_(height), stride_(stride), format_(format),
          vmo_(vmo), buf_(buf), fg_color_(fg_color), bg_color_(bg_color),
          use_intel_y_tiling_(use_intel_y_tiling) {}

Image* Image::Create(zx_handle_t dc_handle,
                     uint32_t width, uint32_t height, zx_pixel_format_t format,
                     uint32_t fg_color, uint32_t bg_color, bool use_intel_y_tiling) {
    zx::channel allocator2_client;
    zx::channel allocator2_server;
    zx_status_t status;
    status = zx::channel::create(0, &allocator2_client, &allocator2_server);
    if (status != ZX_OK) {
        return nullptr;
    }
    status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator", allocator2_server.release());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to connect to sysmem\n");
        return nullptr;
    }

    zx::channel token_client_1;
    zx::channel token_server_1;
    status = zx::channel::create(0, &token_client_1, &token_server_1);

    if (status != ZX_OK) {
        return nullptr;
    }
    status = fuchsia_sysmem_AllocatorAllocateSharedCollection(
        allocator2_client.get(), token_server_1.release());

    if (status != ZX_OK) {
        fprintf(stderr, "Failed to allocate shared collection\n");
        return nullptr;
    }
    zx::channel token_client_2;
    zx::channel token_server_2;
    status = zx::channel::create(0, &token_client_2, &token_server_2);

    if (status != ZX_OK) {
        return nullptr;
    }
    status = fuchsia_sysmem_BufferCollectionTokenDuplicate(
        token_client_1.get(), std::numeric_limits<uint32_t>::max(), token_server_2.release());

    if (status != ZX_OK) {
        fprintf(stderr, "Failed to duplicate token\n");
        return nullptr;
    }

    status = fuchsia_sysmem_BufferCollectionTokenSync(token_client_1.get());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to sync token\n");
        return nullptr;
    }
    fuchsia_hardware_display_ControllerImportBufferCollectionRequest import_msg = {};
    import_msg.hdr.ordinal =
        fuchsia_hardware_display_ControllerImportBufferCollectionOrdinal;
    import_msg.collection_id = 0;
    import_msg.collection_token = FIDL_HANDLE_PRESENT;

    fuchsia_hardware_display_ControllerImportBufferCollectionResponse import_rsp = {};

    zx_handle_t handle = token_client_2.release();
    zx_channel_call_args_t import_call = {};
    import_call.wr_bytes = &import_msg;
    import_call.rd_bytes = &import_rsp;
    import_call.wr_num_bytes = sizeof(import_msg);
    import_call.rd_num_bytes = sizeof(import_rsp);
    import_call.wr_num_handles = 1;
    import_call.wr_handles = &handle;
    uint32_t actual_bytes, actual_handles;
    status = zx_channel_call(dc_handle,
                             0, ZX_TIME_INFINITE, &import_call, &actual_bytes, &actual_handles);
    if (status != ZX_OK || import_rsp.res != ZX_OK) {
        fprintf(stderr, "Failed to import buffer collection\n");
        return nullptr;
    }

    fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsRequest constraints_msg = {};
    constraints_msg.hdr.ordinal =
        fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsOrdinal;
    constraints_msg.config.pixel_format = format;
    constraints_msg.config.height = height;
    constraints_msg.config.width = width;
    if (use_intel_y_tiling) {
        constraints_msg.config.type = 2; // IMAGE_TYPE_Y_LEGACY
    } else {
        constraints_msg.config.type = IMAGE_TYPE_SIMPLE;
    }
    // Planes aren't initialized because they're determined once sysmem has
    // allocated the image.
    constraints_msg.collection_id = 0;

    fuchsia_hardware_display_ControllerSetBufferCollectionConstraintsResponse constraints_rsp = {};
    zx_channel_call_args_t constraints_call = {};
    constraints_call.wr_bytes = &constraints_msg;
    constraints_call.rd_bytes = &constraints_rsp;
    constraints_call.wr_num_bytes = sizeof(constraints_msg);
    constraints_call.rd_num_bytes = sizeof(constraints_rsp);
    status = zx_channel_call(dc_handle,
                             0, ZX_TIME_INFINITE, &constraints_call, &actual_bytes, &actual_handles);
    if (status != ZX_OK || constraints_rsp.res != ZX_OK) {
        fprintf(stderr, "Failed to set constraints\n");
        return nullptr;
    }

    zx::channel collection_client;
    zx::channel collection_server;
    status = zx::channel::create(0, &collection_client, &collection_server);

    status = fuchsia_sysmem_AllocatorBindSharedCollection(
        allocator2_client.get(), token_client_1.release(), collection_server.release());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to bind shared collection\n");
        return nullptr;
    }

    fuchsia_sysmem_BufferCollectionConstraints constraints = {};
    constraints.usage.cpu = fuchsia_sysmem_cpuUsageReadOften | fuchsia_sysmem_cpuUsageWriteOften;
    constraints.min_buffer_count_for_camping = 1;
    constraints.has_buffer_memory_constraints = false;
    constraints.image_format_constraints_count = 1;
    fuchsia_sysmem_ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    if (format == ZX_PIXEL_FORMAT_ARGB_8888 || format == ZX_PIXEL_FORMAT_RGB_x888) {
        image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_BGRA32;
        image_constraints.color_spaces_count = 1;
        image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
            .type = fuchsia_sysmem_ColorSpaceType_SRGB,
        };
    } else {
        image_constraints.pixel_format.type = fuchsia_sysmem_PixelFormatType_NV12;
        image_constraints.color_spaces_count = 1;
        image_constraints.color_space[0] = fuchsia_sysmem_ColorSpace{
            .type = fuchsia_sysmem_ColorSpaceType_REC709,
        };
    }
    if (use_intel_y_tiling) {
        image_constraints.pixel_format.has_format_modifier = true;
        image_constraints.pixel_format.format_modifier.value =
            fuchsia_sysmem_FORMAT_MODIFIER_INTEL_I915_Y_TILED;
    }
    image_constraints.min_coded_width = width;
    image_constraints.max_coded_width = width;
    image_constraints.min_coded_height = height;
    image_constraints.max_coded_height = height;
    image_constraints.min_bytes_per_row = 0;
    image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
    image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 1;
    image_constraints.coded_height_divisor = 1;
    image_constraints.bytes_per_row_divisor = 1;
    image_constraints.start_offset_divisor = 1;
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    status = fuchsia_sysmem_BufferCollectionSetConstraints(
        collection_client.get(), true, &constraints);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to set local constraints\n");
        return nullptr;
    }

    zx_status_t allocation_status;
    fuchsia_sysmem_BufferCollectionInfo_2 buffer_collection_info{};
    status = fuchsia_sysmem_BufferCollectionWaitForBuffersAllocated(
        collection_client.get(), &allocation_status, &buffer_collection_info);

    if (status != ZX_OK) {
        fprintf(stderr, "Failed to wait for buffers allocated\n");
        return nullptr;
    }

    status = fuchsia_sysmem_BufferCollectionClose(collection_client.get());
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to close buffer collection\n");
        return nullptr;
    }

    fuchsia_hardware_display_ControllerReleaseBufferCollectionRequest close_msg = {};
    close_msg.hdr.ordinal =
        fuchsia_hardware_display_ControllerReleaseBufferCollectionOrdinal;
    close_msg.collection_id = 0;

    zx_channel_write(dc_handle, 0, &close_msg, sizeof(close_msg), nullptr, 0);

    uint32_t buffer_size = buffer_collection_info.settings.buffer_settings.size_bytes;
    zx::vmo vmo(buffer_collection_info.buffers[0].vmo);
    uint32_t stride_pixels =
        buffer_collection_info.settings.image_format_constraints.min_bytes_per_row / ZX_PIXEL_FORMAT_BYTES(format);

    uintptr_t addr;
    uint32_t perms = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
    if (zx::vmar::root_self()->map(0, vmo, 0, buffer_size, perms, &addr) != ZX_OK) {
        printf("Failed to map vmar\n");
        return nullptr;
    }

    // We don't expect stride to be much more than width, or expect the buffer
    // to be much more than stride * height, so just fill the whole buffer with
    // bg_color.
    uint32_t* ptr = reinterpret_cast<uint32_t*>(addr);
    for (unsigned i = 0; i < buffer_size / sizeof(uint32_t); i++) {
        ptr[i] = bg_color;
    }
    zx_cache_flush(ptr, buffer_size, ZX_CACHE_FLUSH_DATA);

    return new Image(width, height, stride_pixels, format,
                     vmo.release(), ptr, fg_color, bg_color,
                     use_intel_y_tiling);
}

#define STRIPE_SIZE 37 // prime to make movement more interesting

void Image::Render(int32_t prev_step, int32_t step_num) {
    if (format_ == ZX_PIXEL_FORMAT_NV12) {
        uint32_t byte_stride = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
        uint32_t real_height = height_;
        for (uint32_t y = 0; y < real_height; y++) {
            uint8_t* buf = static_cast<uint8_t*>(buf_) + y * stride_;
            memset(buf, 128, stride_);
        }

        for (uint32_t y = 0; y < real_height / 2; y++) {
            for (uint32_t x = 0; x < width_ / 2; x++) {
                uint8_t* buf =
                    static_cast<uint8_t*>(buf_) + real_height * stride_ + y * stride_ + x * 2;
                int32_t in_stripe = (((x * 2) / STRIPE_SIZE % 2) != ((y * 2) / STRIPE_SIZE % 2));
                if (in_stripe) {
                    buf[0] = 16;
                    buf[1] = 256 - 16;
                } else {
                    buf[0] = 256 - 16;
                    buf[1] = 16;
                }
            }
        }
        zx_cache_flush(reinterpret_cast<uint8_t*>(buf_), byte_stride * height_ * 3 / 2,
                       ZX_CACHE_FLUSH_DATA);
    } else {
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
                int32_t color = in_stripe ? fg_color_ : bg_color_;

                uint32_t* ptr = static_cast<uint32_t*>(buf_);
                if (!use_intel_y_tiling_) {
                    ptr += (y * stride_) + x;
                } else {
                    // Add the offset to the pixel's tile
                    uint32_t width_in_tiles = (width_ + TILE_PIXEL_WIDTH - 1) / TILE_PIXEL_WIDTH;
                    uint32_t tile_idx =
                        (y / TILE_PIXEL_HEIGHT) * width_in_tiles + (x / TILE_PIXEL_WIDTH);
                    ptr += (TILE_NUM_PIXELS * tile_idx);
                    // Add the offset within the pixel's tile
                    uint32_t subtile_column_offset =
                        ((x % TILE_PIXEL_WIDTH) / SUBTILE_COLUMN_WIDTH) * TILE_PIXEL_HEIGHT;
                    uint32_t subtile_line_offset =
                        (subtile_column_offset + (y % TILE_PIXEL_HEIGHT)) * SUBTILE_COLUMN_WIDTH;
                    ptr += subtile_line_offset + (x % SUBTILE_COLUMN_WIDTH);
                }
                *ptr = color;
            }
        }

        if (!use_intel_y_tiling_) {
            uint32_t byte_stride = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
            zx_cache_flush(reinterpret_cast<uint8_t*>(buf_) + (byte_stride * start),
                           byte_stride * (end - start), ZX_CACHE_FLUSH_DATA);
        } else {
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
        }
    }
}

void Image::GetConfig(fuchsia_hardware_display_ImageConfig* config_out) {
    config_out->height = height_;
    config_out->width = width_;
    config_out->pixel_format = format_;
    if (!use_intel_y_tiling_) {
        config_out->type = IMAGE_TYPE_SIMPLE;
    } else {
        config_out->type = 2; // IMAGE_TYPE_Y_LEGACY
    }
    memset(config_out->planes, 0, sizeof(config_out->planes));
    config_out->planes[0].byte_offset = 0;
    config_out->planes[0].bytes_per_row = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
    if (config_out->pixel_format == ZX_PIXEL_FORMAT_NV12) {
        config_out->planes[1].byte_offset = stride_ * height_;
        config_out->planes[1].bytes_per_row = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
    }
}

bool Image::Import(zx_handle_t dc_handle, image_import_t* info_out) {
    for (int i = 0; i < 2; i++) {
        static int event_id = INVALID_ID + 1;
        zx_handle_t e1, e2;
        if (zx_event_create(0, &e1) != ZX_OK
                || zx_handle_duplicate(e1, ZX_RIGHT_SAME_RIGHTS, &e2) != ZX_OK) {
            printf("Failed to create event\n");
            return false;
        }

        fuchsia_hardware_display_ControllerImportEventRequest import_evt_msg;
        import_evt_msg.hdr.ordinal = fuchsia_hardware_display_ControllerImportEventOrdinal;
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

    fuchsia_hardware_display_ControllerImportVmoImageRequest import_msg;
    import_msg.hdr.ordinal = fuchsia_hardware_display_ControllerImportVmoImageOrdinal;
    GetConfig(&import_msg.image_config);
    import_msg.vmo = FIDL_HANDLE_PRESENT;
    import_msg.offset = 0;
    zx_handle_t vmo_dup;
    if (zx_handle_duplicate(vmo_, ZX_RIGHT_SAME_RIGHTS, &vmo_dup) != ZX_OK) {
        printf("Failed to dup handle\n");
        return false;
    }

    fuchsia_hardware_display_ControllerImportVmoImageResponse import_rsp;
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
