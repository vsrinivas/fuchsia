// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera_stream_constraints.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
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
static fuchsia::sysmem::ImageFormat_2 MakeImageFormat(uint32_t width, uint32_t height,
                                                      fuchsia::sysmem::PixelFormatType format) {
  fuchsia::sysmem::PixelFormat pixel_format = {.type = format, .has_format_modifier = false};
  fuchsia_sysmem_PixelFormat pixel_format_c = ConvertPixelFormatToC(pixel_format);
  uint32_t bytes_per_row = ImageFormatStrideBytesPerWidthPixel(&pixel_format_c) * width;

  return {
      .pixel_format = {.type = format, .has_format_modifier = false},
      .coded_width = width,
      .coded_height = height,
      .bytes_per_row = bytes_per_row,
      .display_width = width,
      .display_height = width,
      .layers = 1,
      .color_space = {.type = fuchsia::sysmem::ColorSpaceType::REC601_PAL},
  };
}

void CameraStreamConstraints::AddImageFormat(uint32_t width, uint32_t height,
                                             fuchsia::sysmem::PixelFormatType format) {
  formats_.push_back(MakeImageFormat(width, height, format));
}

fuchsia::camera2::hal::StreamConfig CameraStreamConstraints::ConvertToStreamConfig() {
  // Don't make a stream config if AddImageFormats has not been called.
  ZX_ASSERT(!formats_.empty());
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count_for_camping = buffer_count_for_camping_;
  if (contiguous_) {
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.physically_contiguous_required = true;
    constraints.buffer_memory_constraints.secure_required = false;
  } else {
    constraints.has_buffer_memory_constraints = false;
  }
  if (cpu_access_) {
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageRead;
  } else {
    constraints.usage.video = fuchsia::sysmem::videoUsageCapture;
  }
  // Just make one constraint that has the biggest width/height for each format type:
  // TODO(41321): Map these out. Right now we just use NV12 for everything.
  uint32_t max_width = 0, max_height = 0, max_bytes_per_row = 0;
  for (auto& format : formats_) {
    max_width = std::max(max_width, format.coded_width);
    max_height = std::max(max_height, format.coded_height);
    max_bytes_per_row = std::max(max_bytes_per_row, format.bytes_per_row);
  }
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = {
      .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::NV12},
      .color_spaces_count = 1,
      .color_space =
          {
              {{fuchsia::sysmem::ColorSpaceType::REC601_PAL}},
          },
      .min_coded_width = max_width,
      .max_coded_width = max_width,
      .min_coded_height = max_height,
      .max_coded_height = max_height,
      .min_bytes_per_row = fbl::round_up(max_bytes_per_row, bytes_per_row_divisor_),
      .max_bytes_per_row = 0xfffffff,
      .layers = 1,
      .bytes_per_row_divisor = bytes_per_row_divisor_,
  };
  fuchsia::camera2::StreamProperties stream_properties{};
  stream_properties.set_stream_type(stream_type_);

  return {
      .frame_rate = {.frames_per_sec_numerator = frames_per_second_,
                     .frames_per_sec_denominator = 1},
      .constraints = constraints,
      .properties = std::move(stream_properties),
      .image_formats = fidl::Clone(formats_),
  };
}

}  // namespace camera
