// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_constraints.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/affine/ratio.h>
#include <lib/image-format/image_format.h>

#include <fbl/algorithm.h>

#include "src/camera/lib/format_conversion/format_conversion.h"

namespace camera {

// Make an ImageFormat_2 struct with default values except for width, height and format.
fuchsia::sysmem::ImageFormat_2 StreamConstraints::MakeImageFormat(
    uint32_t width, uint32_t height, fuchsia::sysmem::PixelFormatType format,
    uint32_t original_width, uint32_t original_height) {
  fuchsia::sysmem::PixelFormat pixel_format = {.type = format, .has_format_modifier = false};
  fuchsia_sysmem_PixelFormat pixel_format_c = ConvertPixelFormatToC(pixel_format);
  uint32_t bytes_per_row = ImageFormatStrideBytesPerWidthPixel(&pixel_format_c) * width;

  // All four dimensions must be non-zero to generate a valid aspect ratio.
  bool has_pixel_aspect_ratio = width && height && original_width && original_height;
  uint64_t pixel_aspect_ratio_width = 1;
  uint64_t pixel_aspect_ratio_height = 1;
  if (width == 0 || height == 0) {
    width = 1;
    height = 1;
  }
  if (has_pixel_aspect_ratio) {
    // Calculate the reduced fraction for the rational value (original_ratio / format_ratio).
    // Equivalent to (original_width / original_height) / (width / height)
    //             = (original_width * height) / (original_height * width)
    pixel_aspect_ratio_width = static_cast<uint64_t>(original_width) * height;
    pixel_aspect_ratio_height = static_cast<uint64_t>(original_height) * width;
    affine::Ratio::Reduce(&pixel_aspect_ratio_width, &pixel_aspect_ratio_height);
    // Round and truncate values that are still too large to fit in the format struct.
    while (pixel_aspect_ratio_width > std::numeric_limits<uint32_t>::max() ||
           pixel_aspect_ratio_height > std::numeric_limits<uint32_t>::max()) {
      pixel_aspect_ratio_width = (pixel_aspect_ratio_width + (1u << 31)) >> 1;
      pixel_aspect_ratio_height = (pixel_aspect_ratio_height + (1u << 31)) >> 1;
    }
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
      .has_pixel_aspect_ratio = has_pixel_aspect_ratio,
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
  constraints.min_buffer_count = min_buffer_count_;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = false;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  if (contiguous_) {
    constraints.buffer_memory_constraints.physically_contiguous_required = true;
  }
  // To allow for pinning
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite;
  constraints.usage.video = fuchsia::sysmem::videoUsageCapture;

  // Fully constrain the image dimensions. This will make it more likely that a client's constraints
  // won't conflict with the cameras. Unspecified dimensions default to MIN/MAX which doesn't let
  // AttachToken clients resolve to the same constraints.
  uint32_t max_width = 0, max_height = 0;
  uint32_t min_width = std::numeric_limits<uint32_t>::max();
  uint32_t min_height = std::numeric_limits<uint32_t>::max();
  constexpr uint32_t kCodedHeightDivisor = 2;
  constexpr uint32_t kStartOffsetDivisor = 2;

  for (auto& format : formats_) {
    max_width = std::max(max_width, format.coded_width);
    max_height = std::max(max_height, format.coded_height);
    min_width = std::min(min_width, format.coded_width);
    min_height = std::min(min_height, format.coded_height);
  }
  constraints.image_format_constraints_count = 1;
  constraints.image_format_constraints[0] = {
      .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::NV12},
      .color_spaces_count = 1,
      .color_space =
          {
              {{fuchsia::sysmem::ColorSpaceType::REC601_PAL}},
          },
      .min_coded_width = min_width,
      .max_coded_width = max_width,
      .min_coded_height = min_height,
      .max_coded_height = max_height,
      .min_bytes_per_row = min_width,
      .max_coded_width_times_coded_height = max_width * max_height,
      .coded_width_divisor = bytes_per_row_divisor_,
      .coded_height_divisor = kCodedHeightDivisor,
      .bytes_per_row_divisor = bytes_per_row_divisor_,
      .start_offset_divisor = kStartOffsetDivisor,
      .required_min_coded_width = max_width,
      .required_max_coded_width = max_width,
      .required_min_coded_height = max_height,
      .required_max_coded_height = max_height,
      .required_min_bytes_per_row = max_width,
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
