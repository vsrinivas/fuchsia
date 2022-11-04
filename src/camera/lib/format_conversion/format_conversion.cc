// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format_conversion.h"

#include <lib/image-format/image_format.h>

#include "fidl/fuchsia.sysmem/cpp/wire_types.h"

namespace camera {

fuchsia_sysmem::wire::ImageFormat2 ConvertToWireType(fuchsia::sysmem::ImageFormat_2 image_format) {
  return {
      .pixel_format =
          {
              .type = static_cast<const fuchsia_sysmem::wire::PixelFormatType>(
                  image_format.pixel_format.type),
              .has_format_modifier = image_format.pixel_format.has_format_modifier,
              .format_modifier =
                  {
                      .value = image_format.pixel_format.format_modifier.value,
                  },
          },
      .coded_width = image_format.coded_width,
      .coded_height = image_format.coded_height,
      .bytes_per_row = image_format.bytes_per_row,
      .display_width = image_format.display_width,
      .display_height = image_format.display_height,
      .layers = image_format.layers,
      .color_space =
          {
              .type = static_cast<const fuchsia_sysmem::wire::ColorSpaceType>(
                  image_format.color_space.type),
          },
      .has_pixel_aspect_ratio = image_format.has_pixel_aspect_ratio,
      .pixel_aspect_ratio_width = image_format.pixel_aspect_ratio_width,
      .pixel_aspect_ratio_height = image_format.pixel_aspect_ratio_height,
  };
}

fuchsia_sysmem::wire::ImageFormatConstraints ConvertToWireType(
    fuchsia::sysmem::ImageFormatConstraints constraints) {
  return {
      .pixel_format =
          {
              .type =
                  static_cast<fuchsia_sysmem::wire::PixelFormatType>(constraints.pixel_format.type),
              .has_format_modifier = constraints.pixel_format.has_format_modifier,
              .format_modifier =
                  {
                      .value = constraints.pixel_format.format_modifier.value,
                  },
          },
      .color_spaces_count = constraints.color_spaces_count,
      .color_space =
          [&]() {
            fidl::Array<fuchsia_sysmem::wire::ColorSpace, 32> color_space;
            for (size_t i = 0; i < constraints.color_spaces_count; ++i) {
              color_space[i].type = static_cast<fuchsia_sysmem::wire::ColorSpaceType>(
                  constraints.color_space[i].type);
            }
            return color_space;
          }(),
      .min_coded_width = constraints.min_coded_width,
      .max_coded_width = constraints.max_coded_width,
      .min_coded_height = constraints.min_coded_height,
      .max_coded_height = constraints.max_coded_height,
      .min_bytes_per_row = constraints.min_bytes_per_row,
      .max_bytes_per_row = constraints.max_bytes_per_row,
      .max_coded_width_times_coded_height = constraints.max_coded_width_times_coded_height,
      .layers = constraints.layers,
      .coded_width_divisor = constraints.coded_width_divisor,
      .coded_height_divisor = constraints.coded_height_divisor,
      .bytes_per_row_divisor = constraints.bytes_per_row_divisor,
      .start_offset_divisor = constraints.start_offset_divisor,
      .display_width_divisor = constraints.display_width_divisor,
      .display_height_divisor = constraints.display_height_divisor,
      .required_min_coded_width = constraints.required_min_coded_width,
      .required_max_coded_width = constraints.required_max_coded_width,
      .required_min_coded_height = constraints.required_min_coded_height,
      .required_max_coded_height = constraints.required_max_coded_height,
      .required_min_bytes_per_row = constraints.required_min_bytes_per_row,
      .required_max_bytes_per_row = constraints.required_max_bytes_per_row,
  };
}

fuchsia_sysmem::wire::ImageFormat2 GetImageFormatFromConstraints(
    fuchsia_sysmem::wire::ImageFormatConstraints constraints, uint32_t coded_width,
    uint32_t coded_height) {
  uint32_t bytes_per_row;
  bool success = ImageFormatMinimumRowBytes(constraints, coded_width, &bytes_per_row);
  if (!success) {
    ZX_ASSERT(coded_width > 0);
    ZX_ASSERT(coded_width <= constraints.max_coded_width);
    ZX_ASSERT(coded_width >= constraints.min_coded_width);
    ZX_ASSERT(!constraints.pixel_format.has_format_modifier);
    ZX_ASSERT_MSG(constraints.max_bytes_per_row >= coded_width, "???? %d vs %d",
                  constraints.max_bytes_per_row, coded_width);
  }
  ZX_ASSERT(success);
  return {
      .pixel_format = constraints.pixel_format,
      .coded_width = coded_width,
      .coded_height = coded_height,
      .bytes_per_row = bytes_per_row,
      .display_width = coded_width,
      .display_height = coded_height,
      .layers = 1,
      .color_space = constraints.color_space[0],
  };
}

}  // namespace camera
