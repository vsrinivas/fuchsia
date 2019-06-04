// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_frames.h"

#include <lib/media/codec_impl/fourcc.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <math.h>

#include <fstream>
#include <iostream>
#include <memory>

size_t AlignUp(size_t raw, size_t alignment) {
  return (raw + alignment - 1) / alignment * alignment;
}

// static
std::optional<RawFrames> RawFrames::FromI420File(std::string file,
                                                 RawFrames::Layout layout) {
  size_t file_size;
  std::fstream input_file(file,
                          std::ios::binary | std::ios::in | std::ios::ate);
  if (!input_file.is_open()) {
    fprintf(stderr, "Failed to open %s.\n", file.c_str());
    return std::nullopt;
  }
  file_size = input_file.tellg();
  input_file.seekg(0);

  const size_t source_frame_size = layout.width * layout.height * 3 / 2;
  if (file_size % source_frame_size) {
    fprintf(stderr, "%s is not raw I420 data of the given dimensions.\n",
            file.c_str());
    return std::nullopt;
  }

  const size_t frame_count = file_size / source_frame_size;
  if (frame_count == 0) {
    fprintf(stderr, "%s has no frames in it.\n", file.c_str());
    return std::nullopt;
  }

  const size_t frame_stored_size =
      AlignUp(source_frame_size, layout.frame_alignment);
  const size_t total_storage_size = frame_stored_size * frame_count;

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  zx_status_t err = mapper.CreateAndMap(
      total_storage_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo);
  if (err != ZX_OK) {
    fprintf(stderr, "Failed to create and map vmo: %d\n", err);
    return std::nullopt;
  }

  // We prepare the image in YV12 format, and add padding for each row if
  // `stride` > `width`.
  for (size_t i = 0; i < frame_count; ++i) {
    char* y_start =
        reinterpret_cast<char*>(mapper.start()) + i * frame_stored_size;
    char* v_start = y_start + layout.height * layout.stride;
    char* u_start = v_start + (layout.height / 2) * (layout.stride / 2);
    // Y Plane
    for (size_t j = 0; j < layout.height; ++j) {
      input_file.read(y_start + j * layout.stride, layout.width);
    }
    // U Plane
    for (size_t j = 0; j < layout.height / 2; ++j) {
      input_file.read(u_start + j * (layout.stride / 2), layout.width / 2);
    }
    // V Plane
    for (size_t j = 0; j < layout.height / 2; ++j) {
      input_file.read(v_start + j * (layout.stride / 2), layout.width / 2);
    }
  }

  return RawFrames(layout, std::move(vmo), std::move(mapper), frame_stored_size,
                   frame_count);
}

std::optional<RawFrames::Image> RawFrames::Frame(size_t frame_index) {
  if (frame_index >= frame_count_) {
    return std::nullopt;
  }

  zx::vmo vmo;
  zx_status_t err = frames_.duplicate(
      ZX_RIGHT_READ | ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_MAP,
      &vmo);
  if (err != ZX_OK) {
    fprintf(stderr, "Failed to duplicate frames vmo: %d\n", err);
    return std::nullopt;
  }

  fuchsia::media::VideoUncompressedFormat format = {
      .fourcc = make_fourcc('Y', 'V', '1', '2'),
      .primary_width_pixels = static_cast<uint32_t>(layout_.width),
      .primary_height_pixels = static_cast<uint32_t>(layout_.height),
      .secondary_width_pixels = static_cast<uint32_t>(layout_.width / 2),
      .secondary_height_pixels = static_cast<uint32_t>(layout_.height / 2),
      .planar = true,
      .swizzled = false,
      .primary_line_stride_bytes = static_cast<uint32_t>(layout_.stride),
      .secondary_line_stride_bytes = static_cast<uint32_t>(layout_.stride / 2),
      .primary_start_offset = 0,
      .secondary_start_offset =
          static_cast<uint32_t>(layout_.stride * layout_.height),
      .tertiary_start_offset =
          static_cast<uint32_t>(layout_.stride * layout_.height +
                                layout_.stride / 2 * layout_.height / 2),
      .primary_display_width_pixels = static_cast<uint32_t>(layout_.width),
      .primary_display_height_pixels = static_cast<uint32_t>(layout_.height),
      .pixel_aspect_ratio_width = 1,
      .pixel_aspect_ratio_height = 1,
  };
  const size_t offset = frame_index * frame_stored_size_;

  return {{
      .format = std::move(format),
      .image_size = frame_stored_size_,
      .vmo_offset = offset,
      .vmo = std::move(vmo),
      .image_start = reinterpret_cast<uint8_t*>(mapper_.start()) + offset,
  }};
}

size_t RawFrames::frame_count() const { return frame_count_; }

RawFrames::RawFrames(RawFrames::Layout layout, zx::vmo frames,
                     fzl::VmoMapper mapper, size_t frame_stored_size,
                     size_t frame_count)
    : layout_(std::move(layout)),
      frames_(std::move(frames)),
      mapper_(std::move(mapper)),
      frame_stored_size_(frame_stored_size),
      frame_count_(frame_count) {}
