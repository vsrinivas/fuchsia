// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "format_conversion.h"

#include <lib/image-format/image_format.h>

namespace camera {

fuchsia_sysmem_ImageFormat_2 ConvertHlcppImageFormat2toCType(
    const fuchsia::sysmem::ImageFormat_2& hlcpp_image_format2) {
  return {
      .pixel_format =
          {
              .type = *reinterpret_cast<const fuchsia_sysmem_PixelFormatType*>(
                  &hlcpp_image_format2.pixel_format.type),
              .has_format_modifier = hlcpp_image_format2.pixel_format.has_format_modifier,
              .format_modifier =
                  {
                      .value = hlcpp_image_format2.pixel_format.format_modifier.value,
                  },
          },
      .coded_width = hlcpp_image_format2.coded_width,
      .coded_height = hlcpp_image_format2.coded_height,
      .bytes_per_row = hlcpp_image_format2.bytes_per_row,
      .display_width = hlcpp_image_format2.display_width,
      .display_height = hlcpp_image_format2.display_height,
      .layers = hlcpp_image_format2.layers,
      .color_space = *reinterpret_cast<const fuchsia_sysmem_ColorSpace*>(
          &hlcpp_image_format2.color_space.type),
      .has_pixel_aspect_ratio = hlcpp_image_format2.has_pixel_aspect_ratio,
      .pixel_aspect_ratio_width = hlcpp_image_format2.pixel_aspect_ratio_width,
      .pixel_aspect_ratio_height = hlcpp_image_format2.pixel_aspect_ratio_height,
  };
}

void ConvertToCTypeBufferCollectionInfo2(
    const fuchsia::sysmem::BufferCollectionInfo_2& hlcpp_buffer_collection,
    fuchsia_sysmem_BufferCollectionInfo_2* buffer_collection) {
  buffer_collection->buffer_count = hlcpp_buffer_collection.buffer_count;

  auto& buffer_settings = buffer_collection->settings.buffer_settings;
  auto& hlcpp_buffer_settings = hlcpp_buffer_collection.settings.buffer_settings;
  buffer_settings.size_bytes = hlcpp_buffer_settings.size_bytes;
  buffer_settings.is_physically_contiguous = hlcpp_buffer_settings.is_physically_contiguous;
  buffer_settings.is_secure = hlcpp_buffer_settings.is_secure;
  buffer_settings.coherency_domain = *reinterpret_cast<const fuchsia_sysmem_CoherencyDomain*>(
      &hlcpp_buffer_settings.coherency_domain);
  buffer_settings.heap =
      *reinterpret_cast<const fuchsia_sysmem_HeapType*>(&hlcpp_buffer_settings.heap);
  buffer_collection->settings.has_image_format_constraints =
      hlcpp_buffer_collection.settings.has_image_format_constraints;

  auto& image_format_constraints = buffer_collection->settings.image_format_constraints;
  auto& hlcpp_image_format_constraints = hlcpp_buffer_collection.settings.image_format_constraints;
  image_format_constraints.pixel_format.type =
      *reinterpret_cast<const fuchsia_sysmem_PixelFormatType*>(
          &hlcpp_image_format_constraints.pixel_format.type);
  image_format_constraints.pixel_format.has_format_modifier =
      hlcpp_image_format_constraints.pixel_format.has_format_modifier;
  image_format_constraints.pixel_format.format_modifier.value =
      hlcpp_image_format_constraints.pixel_format.format_modifier.value;

  image_format_constraints.color_spaces_count = hlcpp_image_format_constraints.color_spaces_count;
  for (uint32_t i = 0; i < image_format_constraints.color_spaces_count; ++i) {
    image_format_constraints.color_space[i] = *reinterpret_cast<const fuchsia_sysmem_ColorSpace*>(
        &hlcpp_image_format_constraints.color_space[i].type);
  }

  image_format_constraints.min_coded_width = hlcpp_image_format_constraints.min_coded_width;
  image_format_constraints.max_coded_width = hlcpp_image_format_constraints.max_coded_width;
  image_format_constraints.min_coded_height = hlcpp_image_format_constraints.min_coded_height;
  image_format_constraints.max_coded_height = hlcpp_image_format_constraints.max_coded_height;
  image_format_constraints.min_bytes_per_row = hlcpp_image_format_constraints.min_bytes_per_row;
  image_format_constraints.max_bytes_per_row = hlcpp_image_format_constraints.max_bytes_per_row;

  image_format_constraints.max_coded_width_times_coded_height =
      hlcpp_image_format_constraints.max_coded_width_times_coded_height;
  image_format_constraints.layers = hlcpp_image_format_constraints.layers;
  image_format_constraints.coded_width_divisor = hlcpp_image_format_constraints.coded_width_divisor;
  image_format_constraints.coded_height_divisor =
      hlcpp_image_format_constraints.coded_height_divisor;
  image_format_constraints.bytes_per_row_divisor =
      hlcpp_image_format_constraints.bytes_per_row_divisor;
  image_format_constraints.start_offset_divisor =
      hlcpp_image_format_constraints.start_offset_divisor;
  image_format_constraints.display_width_divisor =
      hlcpp_image_format_constraints.display_width_divisor;
  image_format_constraints.display_height_divisor =
      hlcpp_image_format_constraints.display_height_divisor;
  image_format_constraints.required_min_coded_width =
      hlcpp_image_format_constraints.required_min_coded_width;
  image_format_constraints.required_max_coded_width =
      hlcpp_image_format_constraints.required_max_coded_width;
  image_format_constraints.required_min_coded_height =
      hlcpp_image_format_constraints.required_min_coded_height;
  image_format_constraints.required_max_coded_height =
      hlcpp_image_format_constraints.required_max_coded_height;
  image_format_constraints.required_min_bytes_per_row =
      hlcpp_image_format_constraints.required_min_bytes_per_row;
  image_format_constraints.required_max_bytes_per_row =
      hlcpp_image_format_constraints.required_max_bytes_per_row;

  for (uint32_t i = 0; i < hlcpp_buffer_collection.buffer_count; ++i) {
    buffer_collection->buffers[i].vmo = hlcpp_buffer_collection.buffers[i].vmo.get();
  }
}

fuchsia_sysmem_ImageFormat_2 GetImageFormatFromBufferCollection(
    const fuchsia_sysmem_BufferCollectionInfo_2& buffer_collection, uint32_t coded_width,
    uint32_t coded_height) {
  ZX_ASSERT(buffer_collection.settings.has_image_format_constraints);
  auto& constraints = buffer_collection.settings.image_format_constraints;
  uint32_t bytes_per_row;
  bool success = ImageFormatMinimumRowBytes(&constraints, coded_width, &bytes_per_row);
  if (!success) {
    ZX_ASSERT(coded_width > 0);
    ZX_ASSERT(coded_width <= constraints.max_coded_width);
    ZX_ASSERT(coded_width >= constraints.min_coded_width);
    ZX_ASSERT(!constraints.pixel_format.has_format_modifier);
    ZX_ASSERT_MSG(constraints.max_bytes_per_row >= coded_width, "???? %d vs %d",
                  constraints.max_bytes_per_row, coded_width);
  }
  ZX_ASSERT(success);
  return {.pixel_format = constraints.pixel_format,
          .coded_width = coded_width,
          .coded_height = coded_height,
          .bytes_per_row = bytes_per_row,
          .display_width = coded_width,
          .display_height = coded_height,
          .layers = 1,
          .color_space = constraints.color_space[0]};
}

fuchsia_sysmem_PixelFormat ConvertPixelFormatToC(fuchsia::sysmem::PixelFormat format) {
  fuchsia_sysmem_PixelFormat ret;
  ret.has_format_modifier = format.has_format_modifier;
  // HLCPP and C enum values should always match. Spot-check a single one.
  static_assert(static_cast<fuchsia_sysmem_PixelFormatType>(
                    fuchsia::sysmem::PixelFormatType::YUY2) == fuchsia_sysmem_PixelFormatType_YUY2,
                "HLCPP and C pixel format types don't match.");
  ret.type = static_cast<fuchsia_sysmem_PixelFormatType>(format.type);
  return ret;
}

}  // namespace camera
