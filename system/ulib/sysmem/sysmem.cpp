// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sysmem/sysmem.h>

#include <fbl/algorithm.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fidl/bind.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <lib/syslog/global.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

constexpr char kTag[] = "sysmem";

namespace {

// A helper function to set the plane info for the most common YUV planar formats.
// The width and height fields of |format| must be valid before calling
// this function.
// |format|->layers and |format|->planes will be set.
// The intensity (Y in YUV) is assumed to be present at full resolution, in the
// first plane, with |y_bits_per_pixel| representing each pixel.
// The U and V planes follow.  |uv_together| indicates that U and V are both located on
// the second plane; otherwise U and V are located on planes 2 and 3.
// This function assumes U and V are represented equally.
// |uv_horizontal_bits_per_pixel| indicates how many bits each pixel is represented by
// for a horizontal line only - the vertical subsampling is indicated by |uv_vertical_subsample|,
// so a UV plane that is subsampled 2x2 and U and V are 8 bit interleaved (i.e. NV12)
// (This means for every 2 Y pixels, there is one U byte and one V byte)
// would give uv_horizontal_bits_per_pixel = 4 (8 bits for U and 8 for V for every 2 pixels), and
// a uv_vertical_subsample = 2, to indicate that those 8 bits actually correspond to a set
// of 4 pixels.
// |buffer_size| is set to the total (maximum) image size, rounded up to the nearest page boundry.
void SetImagePlaneInfoPlanarYuv(fuchsia_sysmem_ImageFormat* format,
                                size_t* buffer_size, uint32_t y_bits_per_pixel,
                                uint32_t uv_horizontal_bits_per_pixel,
                                uint32_t uv_vertical_subsample,
                                bool uv_together, bool page_align_layers = false) {
    uint32_t offset;
    format->planes[0].byte_offset = 0;
    format->planes[0].bytes_per_row = (format->width * y_bits_per_pixel) / 8;
    offset = format->planes[0].bytes_per_row * format->height;
    offset = page_align_layers ? fbl::round_up(offset, ZX_PAGE_SIZE) : offset;
    format->planes[1].bytes_per_row = (format->width * uv_horizontal_bits_per_pixel * (uv_together ? 2 : 1)) / 8;
    format->planes[1].byte_offset = offset;
    offset += format->planes[1].bytes_per_row * format->height / uv_vertical_subsample;
    offset = page_align_layers ? fbl::round_up(offset, ZX_PAGE_SIZE) : offset;
    format->layers = 2;
    if (!uv_together) {
        format->layers = 3;
        format->planes[2].bytes_per_row = format->planes[1].bytes_per_row;
        format->planes[2].byte_offset = offset;
        offset += format->planes[2].bytes_per_row * format->height / uv_vertical_subsample;
    }
    *buffer_size = fbl::round_up(offset, ZX_PAGE_SIZE);
}

// A helper function to set the plane info for the most common packed formats.
// The width and height fields of |format| must be valid before calling
// this function.
// |format|->layers and |format|->planes will be set.
// |buffer_size| is set to the total (maximum) image buffer size, rounded up to the nearest page boundry.
void SetImagePlaneInfoPacked(fuchsia_sysmem_ImageFormat* format,
                             size_t* buffer_size, uint32_t bits_per_pixel) {
    format->planes[0].bytes_per_row = (format->width * bits_per_pixel) / 8;
    format->layers = 1;
    *buffer_size = fbl::round_up(format->height * format->planes[0].bytes_per_row, ZX_PAGE_SIZE);
}

zx_status_t PickImageFormat(const fuchsia_sysmem_BufferSpec& spec,
                            fuchsia_sysmem_ImageFormat* format,
                            size_t* buffer_size) {
    // If hardware compatibility needs to be checked, do so here!
    // For the simple case, just use whatever format was specified.
    format->width = spec.image.min_width;
    format->height = spec.image.min_height;
    format->pixel_format = spec.image.pixel_format;
    format->color_space = spec.image.color_space;
    // Need to fill out the plane info, which depends on pixel_format:
    // (More generally, it also depends on color space and BufferUsage,
    // but this is a simplified version.)
    switch (format->pixel_format.type) {
    case fuchsia_sysmem_PixelFormatType_R8G8B8A8:
    case fuchsia_sysmem_PixelFormatType_BGRA32:
        SetImagePlaneInfoPacked(format, buffer_size, 32);
        break;
    case fuchsia_sysmem_PixelFormatType_YUY2:
        SetImagePlaneInfoPacked(format, buffer_size, 16);
        break;
    // NV12 has an NxN Y plane and an interlaced (N/2)x(N/2) U and V plane.
    case fuchsia_sysmem_PixelFormatType_NV12:
        SetImagePlaneInfoPlanarYuv(format, buffer_size, 8, 4, 2, true, false);
        break;
    // I420 has an NxN Y plane and seperate (N/2)x(N/2) U and V planes.
    case fuchsia_sysmem_PixelFormatType_I420:
        SetImagePlaneInfoPlanarYuv(format, buffer_size, 8, 4, 2, false, false);
        break;
    // M420 is interleaved version of I420, with 2 rows of Y and one row of equal size with
    // 2x2 subsampled U and V.  It results in 12 bits per pixel, but since it is organized
    // as height * 1.5 rows, SetImagePlaneInfoPacked will not work if line padding is != 0.
    case fuchsia_sysmem_PixelFormatType_M420:
        SetImagePlaneInfoPacked(format, buffer_size, 12);
        break;
    default:
        FX_LOGF(ERROR, kTag, "Unsupported pixel format %u\n", format->pixel_format.type);
        // static_cast<const uint32_t>(spec.image.pixel_format));
        return ZX_ERR_NOT_SUPPORTED;
    }
    return ZX_OK;
}

} // namespace

static zx_status_t Allocator_AllocateCollection(void* ctx,
                                                uint32_t buffer_count,
                                                const fuchsia_sysmem_BufferSpec* spec,
                                                const fuchsia_sysmem_BufferUsage* usage,
                                                fidl_txn_t* txn) {
    fuchsia_sysmem_BufferCollectionInfo info;
    memset(&info, 0, sizeof(info));
    // Most basic usage of the allocator: create vmos with no special vendor format:
    // 1) Pick which format gets used.  For the simple case, just use whatever format was given.
    //    We also assume here that the format is an ImageFormat
    ZX_ASSERT(info.format.tag == fuchsia_sysmem_BufferSpecTag_image);
    zx_status_t status = PickImageFormat(*spec, &info.format.image, &info.vmo_size);
    if (status != ZX_OK) {
        FX_LOG(ERROR, kTag, "Failed to pick format for Buffer Collection\n");
        return fuchsia_sysmem_AllocatorAllocateCollection_reply(txn, status, &info);
    }

    // 3) Allocate the buffers.  This will be specialized for different formats.
    info.buffer_count = buffer_count;
    for (uint32_t i = 0; i < buffer_count; ++i) {
        status = zx_vmo_create(info.vmo_size, 0, &info.vmos[i]);
        if (status != ZX_OK) {
            // Close the handles we created already.  We do not support partial allocations.
            for (uint32_t j = 0; j < i; ++j) {
                zx_handle_close(info.vmos[j]);
                info.vmos[j] = ZX_HANDLE_INVALID;
            }
            info.buffer_count = 0;
            FX_LOG(ERROR, kTag, "Failed to allocate Buffer Collection\n");
            return fuchsia_sysmem_AllocatorAllocateCollection_reply(txn, ZX_ERR_NO_MEMORY, &info);
        }
    }
    // If everything is happy and allocated, can give ZX_OK:
    return fuchsia_sysmem_AllocatorAllocateCollection_reply(txn, ZX_OK, &info);
}

static zx_status_t Allocator_AllocateSharedCollection(void* ctx,
                                                      uint32_t buffer_count,
                                                      const fuchsia_sysmem_BufferSpec* spec,
                                                      zx_handle_t token_peer,
                                                      fidl_txn_t* txn) {
    return fuchsia_sysmem_AllocatorAllocateSharedCollection_reply(txn, ZX_ERR_NOT_SUPPORTED);
}

static zx_status_t Allocator_BindSharedCollection(void* ctx,
                                                  const fuchsia_sysmem_BufferUsage* usage,
                                                  zx_handle_t token,
                                                  fidl_txn_t* txn) {
    fuchsia_sysmem_BufferCollectionInfo info;
    memset(&info, 0, sizeof(info));
    return fuchsia_sysmem_AllocatorBindSharedCollection_reply(txn, ZX_ERR_NOT_SUPPORTED, &info);
}

static constexpr const fuchsia_sysmem_Allocator_ops_t allocator_ops = {
    .AllocateCollection = Allocator_AllocateCollection,
    .AllocateSharedCollection = Allocator_AllocateSharedCollection,
    .BindSharedCollection = Allocator_BindSharedCollection,
};

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher,
                           const char* service_name, zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_sysmem_Allocator_Name)) {
        return fidl_bind(dispatcher, request,
                         (fidl_dispatch_t*)fuchsia_sysmem_Allocator_dispatch,
                         ctx, &allocator_ops);
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* sysmem_services[] = {
    fuchsia_sysmem_Allocator_Name,
    nullptr,
};

static constexpr zx_service_ops_t sysmem_ops = {
    .init = nullptr,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t sysmem_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = sysmem_services,
    .ops = &sysmem_ops,
};

const zx_service_provider_t* sysmem_get_service_provider() {
    return &sysmem_service_provider;
}
