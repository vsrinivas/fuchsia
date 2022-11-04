// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/sysmem/sysmem.h"

namespace sysmem {

fuchsia_sysmem::wire::PixelFormat banjo_to_fidl(const pixel_format_t& source) {
  return {
      .type = static_cast<fuchsia_sysmem::wire::PixelFormatType>(source.type),
      .has_format_modifier = source.has_format_modifier,
      .format_modifier =
          {
              .value = source.has_format_modifier ? source.format_modifier.value
                                                  : fuchsia_sysmem::wire::kFormatModifierNone,
          },
  };
}

image_format_2_t fidl_to_banjo(const fuchsia_sysmem::wire::ImageFormat2& source) {
  return {
      .pixel_format =
          {
              .type = static_cast<pixel_format_type_t>(source.pixel_format.type),
              .has_format_modifier = source.pixel_format.has_format_modifier,
              .format_modifier =
                  {
                      .value = source.pixel_format.has_format_modifier
                                   ? source.pixel_format.format_modifier.value
                                   : fuchsia_sysmem::wire::kFormatModifierNone,
                  },
          },
      .coded_width = source.coded_width,
      .coded_height = source.coded_height,
      .bytes_per_row = source.bytes_per_row,
      .display_width = source.display_width,
      .display_height = source.display_height,
      .layers = source.layers,
      .color_space = {static_cast<color_space_type_t>(source.color_space.type)},
      .has_pixel_aspect_ratio = source.has_pixel_aspect_ratio,
      .pixel_aspect_ratio_width = source.pixel_aspect_ratio_width,
      .pixel_aspect_ratio_height = source.pixel_aspect_ratio_height,
  };
}

fuchsia_sysmem::wire::ImageFormat2 banjo_to_fidl(const image_format_2_t& source) {
  return {
      .pixel_format = banjo_to_fidl(source.pixel_format),
      .coded_width = source.coded_width,
      .coded_height = source.coded_height,
      .bytes_per_row = source.bytes_per_row,
      .display_width = source.display_width,
      .display_height = source.display_height,
      .layers = source.layers,
      .color_space =
          {
              .type = static_cast<fuchsia_sysmem::wire::ColorSpaceType>(source.color_space.type),
          },
      .has_pixel_aspect_ratio = source.has_pixel_aspect_ratio,
      .pixel_aspect_ratio_width = source.pixel_aspect_ratio_width,
      .pixel_aspect_ratio_height = source.pixel_aspect_ratio_height,
  };
}

buffer_collection_info_2_t fidl_to_banjo(const fuchsia::sysmem::BufferCollectionInfo_2& source) {
  buffer_collection_info_2_t destination = {
      .buffer_count = source.buffer_count,
      .settings =
          {
              .buffer_settings =
                  {
                      .size_bytes = source.settings.buffer_settings.size_bytes,
                      .is_physically_contiguous =
                          source.settings.buffer_settings.is_physically_contiguous,
                      .is_secure = source.settings.buffer_settings.is_secure,
                      .coherency_domain = static_cast<coherency_domain_t>(
                          source.settings.buffer_settings.coherency_domain),
                      .heap = static_cast<heap_type_t>(source.settings.buffer_settings.heap),
                  },
              .has_image_format_constraints = source.settings.has_image_format_constraints,
          },
  };
  if (source.settings.has_image_format_constraints) {
    const fuchsia::sysmem::ImageFormatConstraints& image_format_constraints =
        source.settings.image_format_constraints;
    destination.settings.image_format_constraints = {
        .pixel_format =
            {
                .type =
                    static_cast<pixel_format_type_t>(image_format_constraints.pixel_format.type),
                .has_format_modifier = image_format_constraints.pixel_format.has_format_modifier,
                .format_modifier =
                    {.value = image_format_constraints.pixel_format.has_format_modifier
                                  ? image_format_constraints.pixel_format.format_modifier.value
                                  : fuchsia_sysmem::wire::kFormatModifierNone},
            },
        .color_spaces_count = image_format_constraints.color_spaces_count,
        .min_coded_width = image_format_constraints.min_coded_width,
        .max_coded_width = image_format_constraints.max_coded_width,
        .min_coded_height = image_format_constraints.min_coded_height,
        .max_coded_height = image_format_constraints.max_coded_height,
        .min_bytes_per_row = image_format_constraints.min_bytes_per_row,
        .max_bytes_per_row = image_format_constraints.max_bytes_per_row,
        .max_coded_width_times_coded_height =
            image_format_constraints.max_coded_width_times_coded_height,
        .layers = image_format_constraints.layers,
        .coded_width_divisor = image_format_constraints.coded_width_divisor,
        .coded_height_divisor = image_format_constraints.coded_height_divisor,
        .bytes_per_row_divisor = image_format_constraints.bytes_per_row_divisor,
        .start_offset_divisor = image_format_constraints.start_offset_divisor,
        .display_width_divisor = image_format_constraints.display_width_divisor,
        .display_height_divisor = image_format_constraints.display_height_divisor,
        .required_min_coded_width = image_format_constraints.required_min_coded_width,
        .required_max_coded_width = image_format_constraints.required_max_coded_width,
        .required_min_coded_height = image_format_constraints.required_min_coded_height,
        .required_max_coded_height = image_format_constraints.required_max_coded_height,
        .required_min_bytes_per_row = image_format_constraints.required_min_bytes_per_row,
        .required_max_bytes_per_row = image_format_constraints.required_max_bytes_per_row,
    };
    for (size_t i = 0; i < image_format_constraints.color_spaces_count; i++) {
      destination.settings.image_format_constraints.color_space[i].type =
          static_cast<color_space_type_t>(image_format_constraints.color_space[i].type);
    }
  }
  for (size_t i = 0; i < source.buffer_count; i++) {
    destination.buffers[i].vmo = source.buffers[i].vmo.get();
    destination.buffers[i].vmo_usable_start = source.buffers[i].vmo_usable_start;
  }
  return destination;
}

}  // namespace sysmem
