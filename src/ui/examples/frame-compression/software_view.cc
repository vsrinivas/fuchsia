// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "software_view.h"

#include <lib/trace/event.h>

namespace frame_compression {
namespace {

// sRGB color space.
constexpr uint32_t kColor0 = 0xff6448fe;
constexpr uint32_t kColor1 = 0xffb3d5eb;

}  // namespace

SoftwareView::SoftwareView(scenic::ViewContext context, uint64_t modifier, uint32_t width,
                           uint32_t height, bool paint_once)
    : BaseView(std::move(context), "Software View Example", width, height),
      modifier_(modifier),
      paint_once_(paint_once) {
  zx_status_t status = component_context()->svc()->Connect(sysmem_allocator_.NewRequest());
  FX_CHECK(status == ZX_OK);

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr scenic_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  status = local_token->Sync();
  FX_CHECK(status == ZX_OK);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(scenic_token));

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                   buffer_collection.NewRequest());
  FX_CHECK(status == ZX_OK);

  //
  // Set buffer collection constraints for CPU usage.
  //

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = kNumImages;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.min_size_bytes = 0;
  constraints.buffer_memory_constraints.max_size_bytes = 0xffffffff;
  constraints.buffer_memory_constraints.physically_contiguous_required = false;
  constraints.buffer_memory_constraints.secure_required = false;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.inaccessible_domain_supported = false;
  constraints.buffer_memory_constraints.heap_permitted_count = 0;
  constraints.image_format_constraints_count = 1;
  fuchsia::sysmem::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints = fuchsia::sysmem::ImageFormatConstraints();
  image_constraints.min_coded_width = width_;
  image_constraints.min_coded_height = height_;
  image_constraints.max_coded_width = width_;
  image_constraints.max_coded_height = height_;
  image_constraints.min_bytes_per_row = 0;
  image_constraints.max_bytes_per_row = std::numeric_limits<uint32_t>::max();
  image_constraints.max_coded_width_times_coded_height = std::numeric_limits<uint32_t>::max();
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = modifier_;

  // Force bytes per row to 4 * |width_| when using linear buffer.
  if (modifier_ == fuchsia::sysmem::FORMAT_MODIFIER_LINEAR) {
    image_constraints.min_bytes_per_row = width_ * 4;
    image_constraints.max_bytes_per_row = width_ * 4;
  }

  status = buffer_collection->SetConstraints(true, constraints);
  FX_CHECK(status == ZX_OK);

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_CHECK(status == ZX_OK);
  FX_CHECK(allocation_status == ZX_OK);
  FX_CHECK(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
           image_constraints.pixel_format.type);
  bool needs_flush = buffer_collection_info.settings.buffer_settings.coherency_domain ==
                     fuchsia::sysmem::CoherencyDomain::RAM;
  uint32_t stride = buffer_collection_info.settings.image_format_constraints.min_bytes_per_row;

  //
  // Initialize images from allocated buffer collection.
  //

  for (size_t i = 0; i < kNumImages; ++i) {
    auto& image = images_[i];

    image.image_pipe_id = next_image_pipe_id_++;

    // Add image to |image_pipe_|.
    fuchsia::sysmem::ImageFormat_2 image_format = {};
    image_format.coded_width = width_;
    image_format.coded_height = height_;
    image_pipe_->AddImage(image.image_pipe_id, kBufferId, i, image_format);
    FX_CHECK(allocation_status == ZX_OK);

    uint8_t* vmo_base;
    FX_CHECK(buffer_collection_info.buffers[i].vmo != ZX_HANDLE_INVALID);
    const zx::vmo& image_vmo = buffer_collection_info.buffers[i].vmo;
    auto image_vmo_bytes = buffer_collection_info.settings.buffer_settings.size_bytes;
    FX_CHECK(image_vmo_bytes > 0);
    status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                        ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                        reinterpret_cast<uintptr_t*>(&vmo_base));
    vmo_base += buffer_collection_info.buffers[i].vmo_usable_start;

    image.vmo_ptr = vmo_base;
    image.image_bytes = image_vmo_bytes;
    image.stride = stride;
    image.needs_flush = needs_flush;
  }

  buffer_collection->Close();

  auto& image = images_[GetNextImageIndex()];
  PaintAndPresentImage(image, GetNextColorOffset());
}

void SoftwareView::PaintAndPresentImage(const Image& image, uint32_t color_offset) {
  TRACE_DURATION("gfx", "SoftwareView::PaintAndPresentImage");

  SetPixels(image, color_offset);

  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
  uint64_t now_ns = zx_clock_get_monotonic();
  image_pipe_->PresentImage(image.image_pipe_id, now_ns, std::move(acquire_fences),
                            std::move(release_fences),
                            [this](fuchsia::images::PresentationInfo presentation_info) {
                              if (paint_once_)
                                return;
                              auto& image = images_[GetNextImageIndex()];
                              PaintAndPresentImage(image, GetNextColorOffset());
                            });
}

void SoftwareView::SetPixels(const Image& image, uint32_t color_offset) {
  switch (modifier_) {
    case fuchsia::sysmem::FORMAT_MODIFIER_ARM_AFBC_16X16:
      SetAfbcPixels(image, color_offset);
      break;
    case fuchsia::sysmem::FORMAT_MODIFIER_LINEAR:
      SetLinearPixels(image, color_offset);
      break;
    default:
      FX_NOTREACHED() << "Modifier not supported.";
  }
}

void SoftwareView::SetAfbcPixels(const Image& image, uint32_t color_offset) {
  TRACE_DURATION("gfx", "SoftwareView::SetAfbcPixels");

  uint32_t width_in_tiles = (width_ + kAfbcTilePixelWidth - 1) / kAfbcTilePixelWidth;
  uint32_t height_in_tiles = (height_ + kAfbcTilePixelHeight - 1) / kAfbcTilePixelHeight;
  uint32_t tile_count = width_in_tiles * height_in_tiles;
  uint32_t body_offset =
      ((tile_count * kAfbcBytesPerBlockHeader + kAfbcBodyAlignment - 1) / kAfbcBodyAlignment) *
      kAfbcBodyAlignment;
  uint32_t tile_num_bytes = kAfbcTilePixelWidth * kAfbcTilePixelHeight * kTileBytesPerPixel;
  uint32_t subtile_num_bytes = tile_num_bytes / (kAfbcSubtileSize * kAfbcSubtileSize);
  uint32_t subtile_stride = subtile_num_bytes / kAfbcSubtileSize;

  uint8_t* header_base = image.vmo_ptr;
  uint8_t* body_base = header_base + body_offset;

  for (unsigned j = 0; j < height_in_tiles; j++) {
    unsigned tile_y = j * kAfbcTilePixelHeight;
    unsigned tile_y_end = tile_y + kAfbcTilePixelHeight;

    // Use solid color tile if possible.
    if (tile_y >= color_offset || tile_y_end < color_offset) {
      for (unsigned i = 0; i < width_in_tiles; i++) {
        unsigned header_offset = kAfbcBytesPerBlockHeader * (j * width_in_tiles + i);
        uint8_t* header_ptr = header_base + header_offset;
        uint32_t color = tile_y >= color_offset ? kColor0 : kColor1;

        // Reset header.
        header_ptr[0] = header_ptr[1] = header_ptr[2] = header_ptr[3] = header_ptr[4] =
            header_ptr[5] = header_ptr[6] = header_ptr[7] = header_ptr[12] = header_ptr[13] =
                header_ptr[14] = header_ptr[15] = 0;

        // Solid colors are stored at offset 8 in block header.
        *(reinterpret_cast<uint32_t*>(header_ptr + 8)) = color;
      }
    } else {
      // We only update the first tile in the row and then update all headers
      // for this row to point to the same tile memory. This demonstrates the
      // ability to dedupe tiles that are the same.
      unsigned tile_offset = tile_num_bytes * (j * width_in_tiles);

      // 16 sub-tiles.
      constexpr struct {
        unsigned x;
        unsigned y;
      } kSubtileOffset[kAfbcSubtileSize * kAfbcSubtileSize] = {
          {4, 4}, {0, 4},  {0, 0},   {4, 0},  {8, 0},  {12, 0}, {12, 4}, {8, 4},
          {8, 8}, {12, 8}, {12, 12}, {8, 12}, {4, 12}, {0, 12}, {0, 8},  {4, 8},
      };

      for (unsigned k = 0; k < countof(kSubtileOffset); ++k) {
        unsigned offset = tile_offset + subtile_num_bytes * k;

        for (unsigned yy = 0; yy < kAfbcSubtileSize; ++yy) {
          unsigned y = tile_y + kSubtileOffset[k].y + yy;
          uint32_t color = y >= color_offset ? kColor0 : kColor1;
          uint32_t* target = reinterpret_cast<uint32_t*>(body_base + offset + yy * subtile_stride);

          for (unsigned xx = 0; xx < kAfbcSubtileSize; ++xx) {
            target[xx] = color;
          }
        }
      }

      if (image.needs_flush) {
        zx_cache_flush(body_base + tile_offset, tile_num_bytes, ZX_CACHE_FLUSH_DATA);
      }

      // Update all headers in this row.
      for (unsigned i = 0; i < width_in_tiles; i++) {
        unsigned header_offset = kAfbcBytesPerBlockHeader * (j * width_in_tiles + i);
        uint8_t* header_ptr = header_base + header_offset;

        // Store offset of uncompressed tile memory in byte 0-3.
        uint32_t body_offset = body_base - header_base;
        *(reinterpret_cast<uint32_t*>(header_ptr)) = body_offset + tile_offset;

        // Set byte 4-15 to disable compression for tile memory.
        header_ptr[4] = header_ptr[7] = header_ptr[10] = header_ptr[13] = 0x41;
        header_ptr[5] = header_ptr[8] = header_ptr[11] = header_ptr[14] = 0x10;
        header_ptr[6] = header_ptr[9] = header_ptr[12] = header_ptr[15] = 0x04;
      }
    }
  }

  if (image.needs_flush) {
    zx_cache_flush(header_base, kAfbcBytesPerBlockHeader * width_in_tiles * height_in_tiles,
                   ZX_CACHE_FLUSH_DATA);
  }
}

void SoftwareView::SetLinearPixels(const Image& image, uint32_t color_offset) {
  TRACE_DURATION("gfx", "SoftwareView::SetLinearPixels");

  uint8_t* vmo_base = image.vmo_ptr;
  for (uint32_t y = 0; y < height_; ++y) {
    uint32_t color = y >= color_offset ? kColor0 : kColor1;
    uint32_t* target = reinterpret_cast<uint32_t*>(&vmo_base[y * image.stride]);
    for (uint32_t x = 0; x < width_; ++x) {
      target[x] = color;
    }
  }
  if (image.needs_flush) {
    zx_cache_flush(image.vmo_ptr, image.image_bytes, ZX_CACHE_FLUSH_DATA);
  }
}

}  // namespace frame_compression
