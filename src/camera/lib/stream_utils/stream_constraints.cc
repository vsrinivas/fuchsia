// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_constraints.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/affine/ratio.h>
#include <lib/image-format/image_format.h>

#include <algorithm>
#include <vector>

#include <fbl/algorithm.h>

namespace camera {

static fuchsia_sysmem_PixelFormat ConvertPixelFormatToC(fuchsia::sysmem::PixelFormat format) {
  fuchsia_sysmem_PixelFormat ret;
  ret.has_format_modifier = format.has_format_modifier;
  switch (format.type) {
    case fuchsia::sysmem::PixelFormatType::INVALID:
      ret.type = fuchsia_sysmem_PixelFormatType_INVALID;
      break;
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      ret.type = fuchsia_sysmem_PixelFormatType_R8G8B8A8;
      break;
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      ret.type = fuchsia_sysmem_PixelFormatType_BGRA32;
      break;
    case fuchsia::sysmem::PixelFormatType::I420:
      ret.type = fuchsia_sysmem_PixelFormatType_I420;
      break;
    case fuchsia::sysmem::PixelFormatType::M420:
      ret.type = fuchsia_sysmem_PixelFormatType_M420;
      break;
    case fuchsia::sysmem::PixelFormatType::NV12:
      ret.type = fuchsia_sysmem_PixelFormatType_NV12;
      break;
    case fuchsia::sysmem::PixelFormatType::YUY2:
      ret.type = fuchsia_sysmem_PixelFormatType_YUY2;
      break;
    case fuchsia::sysmem::PixelFormatType::MJPEG:
      ret.type = fuchsia_sysmem_PixelFormatType_MJPEG;
      break;
    case fuchsia::sysmem::PixelFormatType::YV12:
      ret.type = fuchsia_sysmem_PixelFormatType_YV12;
      break;
    case fuchsia::sysmem::PixelFormatType::BGR24:
      ret.type = fuchsia_sysmem_PixelFormatType_BGR24;
      break;
    case fuchsia::sysmem::PixelFormatType::RGB565:
      ret.type = fuchsia_sysmem_PixelFormatType_RGB565;
      break;
    case fuchsia::sysmem::PixelFormatType::RGB332:
      ret.type = fuchsia_sysmem_PixelFormatType_RGB332;
      break;
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      ret.type = fuchsia_sysmem_PixelFormatType_RGB2220;
      break;
    case fuchsia::sysmem::PixelFormatType::L8:
      ret.type = fuchsia_sysmem_PixelFormatType_L8;
      break;
  }
  return ret;
}

// Make an ImageFormat_2 struct with default values except for width, height and format.
fuchsia::sysmem::ImageFormat_2 StreamConstraints::MakeImageFormat(
    uint32_t width, uint32_t height, fuchsia::sysmem::PixelFormatType format,
    uint32_t original_width, uint32_t original_height) {
  fuchsia::sysmem::PixelFormat pixel_format = {.type = format, .has_format_modifier = false};
  fuchsia_sysmem_PixelFormat pixel_format_c = ConvertPixelFormatToC(pixel_format);
  uint32_t bytes_per_row = ImageFormatStrideBytesPerWidthPixel(&pixel_format_c) * width;
  if (width == 0 || height == 0) {
    width = 1;
    height = 1;
  }
  if (original_width == 0) {
    original_width = width;
  }
  if (original_height == 0) {
    original_height = height;
  }
  // Calculate the reduced fraction for the rational value (original_ratio / format_ratio).
  // Equivalent to (original_width / original_height) / (width / height)
  //             = (original_width * height) / (original_height * width)
  auto pixel_aspect_ratio_width = static_cast<uint64_t>(original_width) * height;
  auto pixel_aspect_ratio_height = static_cast<uint64_t>(original_height) * width;
  affine::Ratio::Reduce(&pixel_aspect_ratio_width, &pixel_aspect_ratio_height);
  // Round and truncate values that are still too large to fit in the format struct.
  while (pixel_aspect_ratio_width > std::numeric_limits<uint32_t>::max() ||
         pixel_aspect_ratio_height > std::numeric_limits<uint32_t>::max()) {
    pixel_aspect_ratio_width = (pixel_aspect_ratio_width + (1u << 31)) >> 1;
    pixel_aspect_ratio_height = (pixel_aspect_ratio_height + (1u << 31)) >> 1;
  }
  return {
      .pixel_format = {.type = format, .has_format_modifier = false},
      .coded_width = width,
      .coded_height = height,
      .bytes_per_row = bytes_per_row,
      .display_width = width,
      .display_height = height,
      .layers = 1,
      .color_space = {.type = fuchsia::sysmem::ColorSpaceType::REC601_PAL},
      .pixel_aspect_ratio_width = static_cast<uint32_t>(pixel_aspect_ratio_width),
      .pixel_aspect_ratio_height = static_cast<uint32_t>(pixel_aspect_ratio_height),
  };
}

void StreamConstraints::AddImageFormat(uint32_t width, uint32_t height,
                                       fuchsia::sysmem::PixelFormatType format,
                                       uint32_t original_width, uint32_t original_height) {
  formats_.push_back(MakeImageFormat(width, height, format, original_width, original_height));
}

fuchsia::sysmem::BufferCollectionConstraints StreamConstraints::MakeBufferCollectionConstraints()
    const {
  // Don't make a stream config if AddImageFormats has not been called.
  ZX_ASSERT(!formats_.empty());
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = buffer_count_for_camping_;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  if (contiguous_) {
    constraints.buffer_memory_constraints.physically_contiguous_required = true;
  }
  if (cpu_access_) {
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  } else {
    constraints.usage.video = fuchsia::sysmem::videoUsageCapture;
  }
  // Just make one constraint that has the biggest width/height for each format type:
  // TODO(fxbug.dev/41321): Map these out. Right now we just use NV12 for everything.
  uint32_t max_width = 0, max_height = 0;
  for (auto& format : formats_) {
    max_width = std::max(max_width, format.coded_width);
    max_height = std::max(max_height, format.coded_height);
  }
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = {
      .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::NV12},
      .color_spaces_count = 1,
      .color_space =
          {
              {{fuchsia::sysmem::ColorSpaceType::REC601_PAL}},
          },
      .bytes_per_row_divisor = bytes_per_row_divisor_,
      .required_max_coded_width = max_width,
      .required_max_coded_height = max_height,
  };
  return constraints;
}

fuchsia::camera2::hal::StreamConfig StreamConstraints::ConvertToStreamConfig() {
  // Don't make a stream config if AddImageFormats has not been called.
  ZX_ASSERT(!formats_.empty());
  fuchsia::camera2::StreamProperties stream_properties{};
  stream_properties.set_stream_type(stream_type_);

  for (auto& format : formats_) {
    format.bytes_per_row = fbl::round_up(format.coded_width, bytes_per_row_divisor_);
  }

  return {
      .frame_rate = {.frames_per_sec_numerator = frames_per_second_,
                     .frames_per_sec_denominator = 1},
      .constraints = MakeBufferCollectionConstraints(),
      .properties = std::move(stream_properties),
      .image_formats = fidl::Clone(formats_),
  };
}

}  // namespace camera
