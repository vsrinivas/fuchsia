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

#include <algorithm>
#include <limits>

#include <ddk/protocol/display/controller.h>
#include <fbl/algorithm.h>

#include "utils.h"

static constexpr uint32_t kRenderPeriod = 120;

// TODO(reveman): Add sysmem helper functions instead of duplicating these constants.
static constexpr uint32_t kIntelTilePixelWidth = 32u;
static constexpr uint32_t kIntelTilePixelHeight = 32u;

static constexpr uint32_t kAfbcBodyAlignment = 1024u;
static constexpr uint32_t kAfbcBytesPerBlockHeader = 16u;
static constexpr uint32_t kAfbcTilePixelWidth = 16u;
static constexpr uint32_t kAfbcTilePixelHeight = 16u;

namespace sysmem = ::llcpp::fuchsia::sysmem;
namespace fhd = ::llcpp::fuchsia::hardware::display;

namespace testing {
namespace display {

Image::Image(uint32_t width, uint32_t height, int32_t stride, zx_pixel_format_t format,
             uint32_t collection_id, void* buf, uint32_t fg_color, uint32_t bg_color,
             uint64_t modifier)
    : width_(width),
      height_(height),
      stride_(stride),
      format_(format),
      collection_id_(collection_id),
      buf_(buf),
      fg_color_(fg_color),
      bg_color_(bg_color),
      modifier_(modifier) {}

Image* Image::Create(fhd::Controller::SyncClient* dc, uint32_t width, uint32_t height,
                     zx_pixel_format_t format, uint32_t fg_color, uint32_t bg_color,
                     uint64_t modifier) {
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

  static uint32_t next_collection_id = fhd::INVALID_DISP_ID + 1;
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
  image_constraints.pixel_format.format_modifier.value = modifier;

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
  if (modifier == sysmem::FORMAT_MODIFIER_LINEAR) {
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
  if (zx::vmar::root_self()->map(perms, 0, vmo, 0, buffer_size, &addr) != ZX_OK) {
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
  if (modifier == sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16) {
    uint32_t width_in_tiles = (width + kAfbcTilePixelWidth - 1) / kAfbcTilePixelWidth;
    uint32_t height_in_tiles = (height + kAfbcTilePixelHeight - 1) / kAfbcTilePixelHeight;
    uint32_t tile_count = width_in_tiles * height_in_tiles;
    // Initialize all block headers to |bg_color|.
    for (unsigned i = 0; i < tile_count; ++i) {
      unsigned offset = i * kAfbcBytesPerBlockHeader / sizeof(uint32_t);
      ptr[offset + 0] = 0;
      ptr[offset + 1] = 0;
      // Solid colors are stored as R8G8B8A8 starting at offset 8 in block header.
      ptr[offset + 2] = bg_color;
      ptr[offset + 3] = 0;
    }
  }
  zx_cache_flush(ptr, buffer_size, ZX_CACHE_FLUSH_DATA);

  return new Image(width, height, stride_pixels, format, collection_id, ptr, fg_color, bg_color,
                   modifier);
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
      start = std::min(cur, prev);
      end = std::max(cur, prev);
      draw_stripe = cur > prev;
    }

    if (modifier_ == sysmem::FORMAT_MODIFIER_LINEAR) {
      for (unsigned y = start; y < end; y++) {
        for (unsigned x = 0; x < width_; x++) {
          int32_t in_stripe = draw_stripe && ((x / STRIPE_SIZE % 2) != (y / STRIPE_SIZE % 2));
          int32_t color = in_stripe ? fg_color_ : bg_color_;

          uint32_t* ptr = static_cast<uint32_t*>(buf_) + (y * stride_) + x;
          *ptr = color;
        }
      }
      uint32_t byte_stride = stride_ * ZX_PIXEL_FORMAT_BYTES(format_);
      zx_cache_flush(reinterpret_cast<uint8_t*>(buf_) + (byte_stride * start),
                     byte_stride * (end - start), ZX_CACHE_FLUSH_DATA);
    } else {
      constexpr uint32_t kTileBytesPerPixel = 4u;

      uint32_t tile_pixel_width = 0u;
      uint32_t tile_pixel_height = 0u;
      uint8_t* body = nullptr;
      switch (modifier_) {
        case sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED: {
          tile_pixel_width = kIntelTilePixelWidth;
          tile_pixel_height = kIntelTilePixelHeight;
          body = static_cast<uint8_t*>(buf_);
        } break;
        case sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16: {
          tile_pixel_width = kAfbcTilePixelWidth;
          tile_pixel_height = kAfbcTilePixelHeight;
          uint32_t width_in_tiles = (width_ + tile_pixel_width - 1) / tile_pixel_width;
          uint32_t height_in_tiles = (height_ + tile_pixel_height - 1) / tile_pixel_height;
          uint32_t tile_count = width_in_tiles * height_in_tiles;
          uint32_t body_offset = ((tile_count * kAfbcBytesPerBlockHeader + kAfbcBodyAlignment - 1) /
                                  kAfbcBodyAlignment) *
                                 kAfbcBodyAlignment;
          body = static_cast<uint8_t*>(buf_) + body_offset;
        } break;
        default:
          // Not reached.
          assert(0);
      }

      uint32_t tile_num_bytes = tile_pixel_width * tile_pixel_height * kTileBytesPerPixel;
      uint32_t tile_num_pixels = tile_num_bytes / kTileBytesPerPixel;
      uint32_t width_in_tiles = (width_ + tile_pixel_width - 1) / tile_pixel_width;

      for (unsigned y = start; y < end; y++) {
        for (unsigned x = 0; x < width_; x++) {
          int32_t in_stripe = draw_stripe && ((x / STRIPE_SIZE % 2) != (y / STRIPE_SIZE % 2));
          int32_t color = in_stripe ? fg_color_ : bg_color_;

          uint32_t* ptr = reinterpret_cast<uint32_t*>(body);
          {
            // Add the offset to the pixel's tile
            uint32_t tile_idx = (y / tile_pixel_height) * width_in_tiles + (x / tile_pixel_width);
            ptr += (tile_num_pixels * tile_idx);
            switch (modifier_) {
              case sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED: {
                constexpr uint32_t kSubtileColumnWidth = 4u;
                // Add the offset within the pixel's tile
                uint32_t subtile_column_offset =
                    ((x % tile_pixel_width) / kSubtileColumnWidth) * tile_pixel_height;
                uint32_t subtile_line_offset =
                    (subtile_column_offset + (y % tile_pixel_height)) * kSubtileColumnWidth;
                ptr += subtile_line_offset + (x % kSubtileColumnWidth);
              } break;
              case sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16: {
                constexpr uint32_t kAfbcSubtileOffset[4][4] = {
                    {2u, 1u, 14u, 13u},
                    {3u, 0u, 15u, 12u},
                    {4u, 7u, 8u, 11u},
                    {5u, 6u, 9u, 10u},
                };
                constexpr uint32_t kAfbcSubtileWidth = 4u;
                constexpr uint32_t kAfbcSubtileHeight = 4u;
                uint32_t subtile_num_pixels = kAfbcSubtileWidth * kAfbcSubtileHeight;
                uint32_t subtile_x = (x % tile_pixel_width) / kAfbcSubtileWidth;
                uint32_t subtile_y = (y % tile_pixel_height) / kAfbcSubtileHeight;
                ptr += kAfbcSubtileOffset[subtile_x][subtile_y] * subtile_num_pixels +
                       (y % kAfbcSubtileHeight) * kAfbcSubtileWidth + (x % kAfbcSubtileWidth);
              } break;
              default:
                // Not reached.
                assert(0);
            }
          }
          *ptr = color;
        }
      }

      uint32_t y_start_tile = start / tile_pixel_height;
      uint32_t y_end_tile = (end + tile_pixel_height - 1) / tile_pixel_height;
      for (unsigned i = 0; i < width_in_tiles; i++) {
        for (unsigned j = y_start_tile; j < y_end_tile; j++) {
          unsigned offset = (tile_num_bytes * (j * width_in_tiles + i));
          zx_cache_flush(body + offset, tile_num_bytes, ZX_CACHE_FLUSH_DATA);

          // We also need to update block header when using AFBC.
          if (modifier_ == sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16) {
            unsigned hdr_offset = kAfbcBytesPerBlockHeader * (j * width_in_tiles + i);
            uint8_t* hdr_ptr = reinterpret_cast<uint8_t*>(buf_) + hdr_offset;
            // Store offset of uncompressed tile memory in byte 0-3.
            uint32_t body_offset = body - reinterpret_cast<uint8_t*>(buf_);
            *(reinterpret_cast<uint32_t*>(hdr_ptr)) = body_offset + offset;
            // Set byte 4-15 to disable compression for tile memory.
            hdr_ptr[4] = hdr_ptr[7] = hdr_ptr[10] = hdr_ptr[13] = 0x41;
            hdr_ptr[5] = hdr_ptr[8] = hdr_ptr[11] = hdr_ptr[14] = 0x10;
            hdr_ptr[6] = hdr_ptr[9] = hdr_ptr[12] = hdr_ptr[15] = 0x04;
            zx_cache_flush(hdr_ptr, kAfbcBytesPerBlockHeader, ZX_CACHE_FLUSH_DATA);
          }
        }
      }
    }
  }
}

void Image::GetConfig(fhd::ImageConfig* config_out) {
  config_out->height = height_;
  config_out->width = width_;
  config_out->pixel_format = format_;
  if (modifier_ != sysmem::FORMAT_MODIFIER_INTEL_I915_Y_TILED) {
    config_out->type = IMAGE_TYPE_SIMPLE;
  } else {
    config_out->type = 2;  // IMAGE_TYPE_Y_LEGACY
  }
}

bool Image::Import(fhd::Controller::SyncClient* dc, image_import_t* info_out) {
  for (int i = 0; i < 2; i++) {
    static int event_id = fhd::INVALID_DISP_ID + 1;
    zx::event e1, e2;
    if (zx::event::create(0, &e1) != ZX_OK || e1.duplicate(ZX_RIGHT_SAME_RIGHTS, &e2) != ZX_OK) {
      printf("Failed to create event\n");
      return false;
    }

    info_out->events[i] = std::move(e1);
    info_out->event_ids[i] = event_id;
    dc->ImportEvent(std::move(e2), event_id++);

    if (i != WAIT_EVENT) {
      info_out->events[i].signal(0, ZX_EVENT_SIGNALED);
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

}  // namespace display
}  // namespace testing
