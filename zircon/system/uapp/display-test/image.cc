// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image.h"

#include <fuchsia/hardware/display/llcpp/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/fidl/txn_header.h>
#include <lib/image-format-llcpp/image-format-llcpp.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <limits>

#include <ddk/protocol/display/controller.h>
#include <fbl/algorithm.h>

#include "utils.h"

static constexpr uint32_t kRenderPeriod = 120;

namespace sysmem = ::llcpp::fuchsia::sysmem;
namespace fhd = ::llcpp::fuchsia::hardware::display;

Image::Image(uint32_t width, uint32_t height, int32_t stride, zx_pixel_format_t format,
             uint32_t collection_id, void* buf, uint32_t fg_color, uint32_t bg_color,
             bool use_intel_y_tiling)
    : width_(width),
      height_(height),
      stride_(stride),
      format_(format),
      collection_id_(collection_id),
      buf_(buf),
      fg_color_(fg_color),
      bg_color_(bg_color),
      use_intel_y_tiling_(use_intel_y_tiling) {}

Image* Image::Create(fhd::Controller::SyncClient* dc, uint32_t width, uint32_t height,
                     zx_pixel_format_t format, uint32_t fg_color, uint32_t bg_color,
                     bool use_intel_y_tiling) {
  std::unique_ptr<sysmem::Allocator::SyncClient> allocator;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK ||
        fdio_service_connect("/svc/fuchsia.sysmem.Allocator", server.release()) != ZX_OK) {
      fprintf(stderr, "Failed to connect to sysmem\n");
      return nullptr;
    }
    allocator = std::make_unique<sysmem::Allocator::SyncClient>(std::move(client));
  }

  std::unique_ptr<sysmem::BufferCollectionToken::SyncClient> token;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK ||
        !allocator->AllocateSharedCollection(std::move(server)).ok()) {
      fprintf(stderr, "Failed to allocate shared collection\n");
      return nullptr;
    }
    token = std::make_unique<sysmem::BufferCollectionToken::SyncClient>(std::move(client));
  }
  zx_handle_t display_token_handle;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK ||
        !token->Duplicate(/*rights_attenuation_mask=*/0xffffffff, std::move(server)).ok()) {
      fprintf(stderr, "Failed to duplicate token\n");
      return nullptr;
    }
    display_token_handle = client.release();
  }

  static uint32_t next_collection_id = fhd::invalidId + 1;
  uint32_t collection_id = next_collection_id++;
  if (!token->Sync().ok()) {
    fprintf(stderr, "Failed to sync token\n");
    return nullptr;
  }
  auto import_result = dc->ImportBufferCollection(collection_id, zx::channel(display_token_handle));
  if (!import_result.ok() || import_result->res != ZX_OK) {
    fprintf(stderr, "Failed to import buffer collection\n");
    return nullptr;
  }

  fhd::ImageConfig image_config = {};
  image_config.pixel_format = format;
  image_config.height = height;
  image_config.width = width;
  image_config.type = 0;  // 0 for any image type.
  auto set_constraints_result = dc->SetBufferCollectionConstraints(collection_id, image_config);
  if (!set_constraints_result.ok() || set_constraints_result->res != ZX_OK) {
    fprintf(stderr, "Failed to set constraints\n");
    return nullptr;
  }

  std::unique_ptr<sysmem::BufferCollection::SyncClient> collection;
  {
    zx::channel client, server;
    if (zx::channel::create(0, &client, &server) != ZX_OK ||
        !allocator->BindSharedCollection(std::move(*token->mutable_channel()), std::move(server))
             .ok()) {
      fprintf(stderr, "Failed to bind shared collection\n");
      return nullptr;
    }
    collection = std::make_unique<sysmem::BufferCollection::SyncClient>(std::move(client));
  }

  sysmem::BufferCollectionConstraints constraints = {};
  constraints.usage.cpu = sysmem::cpuUsageReadOften | sysmem::cpuUsageWriteOften;
  constraints.min_buffer_count_for_camping = 1;
  constraints.has_buffer_memory_constraints = true;
  sysmem::BufferMemoryConstraints& buffer_constraints = constraints.buffer_memory_constraints;
  buffer_constraints.ram_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  sysmem::ImageFormatConstraints& image_constraints = constraints.image_format_constraints[0];
  if (format == ZX_PIXEL_FORMAT_ARGB_8888 || format == ZX_PIXEL_FORMAT_RGB_x888) {
    image_constraints.pixel_format.type = sysmem::PixelFormatType::BGRA32;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = sysmem::ColorSpace{
        .type = sysmem::ColorSpaceType::SRGB,
    };
  } else if (format == ZX_PIXEL_FORMAT_ABGR_8888 || format == ZX_PIXEL_FORMAT_BGR_888x) {
    image_constraints.pixel_format.type = sysmem::PixelFormatType::R8G8B8A8;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = sysmem::ColorSpace{
        .type = sysmem::ColorSpaceType::SRGB,
    };
  } else {
    image_constraints.pixel_format.type = sysmem::PixelFormatType::NV12;
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] = sysmem::ColorSpace{
        .type = sysmem::ColorSpaceType::REC709,
    };
  }
  image_constraints.pixel_format.has_format_modifier = true;
  if (use_intel_y_tiling) {
    image_constraints.pixel_format.format_modifier.value =
        sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED;
  } else {
    image_constraints.pixel_format.format_modifier.value = sysmem::FORMAT_MODIFIER_LINEAR;
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

  if (!collection->SetConstraints(true, constraints).ok()) {
    fprintf(stderr, "Failed to set local constraints\n");
    return nullptr;
  }

  auto info_result = collection->WaitForBuffersAllocated();
  if (!info_result.ok() || info_result->status != ZX_OK) {
    fprintf(stderr, "Failed to wait for buffers allocated\n");
    return nullptr;
  }

  if (!collection->Close().ok()) {
    fprintf(stderr, "Failed to close buffer collection\n");
    return nullptr;
  }

  auto& buffer_collection_info = info_result->buffer_collection_info;
  uint32_t buffer_size = buffer_collection_info.settings.buffer_settings.size_bytes;
  zx::vmo vmo(std::move(buffer_collection_info.buffers[0].vmo));

  uint32_t minimum_row_bytes;
  if (!use_intel_y_tiling) {
    bool result = image_format::GetMinimumRowBytes(
        buffer_collection_info.settings.image_format_constraints, width, &minimum_row_bytes);
    if (!result) {
      fprintf(stderr, "Could not calcualte minimum row byte\n");
      return nullptr;
    }
  } else {
    minimum_row_bytes = buffer_collection_info.settings.image_format_constraints.min_bytes_per_row;
  }

  uint32_t stride_pixels = minimum_row_bytes / ZX_PIXEL_FORMAT_BYTES(format);
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

  return new Image(width, height, stride_pixels, format, collection_id, ptr, fg_color, bg_color,
                   use_intel_y_tiling);
}

#define STRIPE_SIZE 37  // prime to make movement more interesting

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
        uint8_t* buf = static_cast<uint8_t*>(buf_) + real_height * stride_ + y * stride_ + x * 2;
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
          uint32_t tile_idx = (y / TILE_PIXEL_HEIGHT) * width_in_tiles + (x / TILE_PIXEL_WIDTH);
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

void Image::GetConfig(fhd::ImageConfig* config_out) {
  config_out->height = height_;
  config_out->width = width_;
  config_out->pixel_format = format_;
  if (!use_intel_y_tiling_) {
    config_out->type = IMAGE_TYPE_SIMPLE;
  } else {
    config_out->type = 2;  // IMAGE_TYPE_Y_LEGACY
  }
  config_out->planes = {};
  config_out->planes[0].byte_offset = 0;
  config_out->planes[0].bytes_per_row = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
  if (config_out->pixel_format == ZX_PIXEL_FORMAT_NV12) {
    config_out->planes[1].byte_offset = stride_ * height_;
    config_out->planes[1].bytes_per_row = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
  }
}

bool Image::Import(fhd::Controller::SyncClient* dc, image_import_t* info_out) {
  for (int i = 0; i < 2; i++) {
    static int event_id = fhd::invalidId + 1;
    zx_handle_t e1, e2;
    if (zx_event_create(0, &e1) != ZX_OK ||
        zx_handle_duplicate(e1, ZX_RIGHT_SAME_RIGHTS, &e2) != ZX_OK) {
      printf("Failed to create event\n");
      return false;
    }

    info_out->events[i] = e1;
    info_out->event_ids[i] = event_id;
    dc->ImportEvent(zx::event(e2), event_id++);

    if (i != WAIT_EVENT) {
      zx_object_signal(e1, 0, ZX_EVENT_SIGNALED);
    }
  }

  fhd::ImageConfig image_config;
  GetConfig(&image_config);
  auto import_result = dc->ImportImage(image_config, collection_id_, /*index=*/0);
  if (!import_result.ok() || import_result->res != ZX_OK) {
    printf("Failed to import image\n");
    return false;
  }
  info_out->id = import_result->image_id;

  // image has been imported. we can close the connection
  dc->ReleaseBufferCollection(collection_id_);
  return true;
}
