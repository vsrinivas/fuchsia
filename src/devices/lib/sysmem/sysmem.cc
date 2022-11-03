// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/sysmem/sysmem.h"

namespace sysmem {

template <typename PixelFormat>
static void pixel_format_banjo_from_fidl(const PixelFormat& source, pixel_format_t& destination) {
  static_assert(std::is_same_v<PixelFormat, fuchsia_sysmem::wire::PixelFormat> ||
                std::is_same_v<PixelFormat, fuchsia_sysmem_PixelFormat>);
  destination.type = static_cast<pixel_format_type_t>(source.type);
  destination.has_format_modifier = source.has_format_modifier;
  destination.format_modifier = {fuchsia_sysmem::wire::kFormatModifierNone};
  if (source.has_format_modifier) {
    destination.format_modifier = {source.format_modifier.value};
  }
}

void pixel_format_fidl_from_banjo(const pixel_format_t& source,
                                  fuchsia_sysmem_PixelFormat& destination) {
  destination.type = source.type;
  destination.has_format_modifier = source.has_format_modifier;
  destination.format_modifier = {fuchsia_sysmem_FORMAT_MODIFIER_NONE};
  if (source.has_format_modifier) {
    destination.format_modifier = {source.format_modifier.value};
  }
}

void pixel_format_fidl_from_banjo(const pixel_format_t& source,
                                  fuchsia_sysmem::wire::PixelFormat& destination) {
  destination.type = fuchsia_sysmem::wire::PixelFormatType(source.type);
  destination.has_format_modifier = source.has_format_modifier;
  destination.format_modifier = {fuchsia_sysmem::wire::kFormatModifierNone};
  if (source.has_format_modifier) {
    destination.format_modifier = {source.format_modifier.value};
  }
}

static void vmo_buffer_banjo_from_fidl(const fuchsia_sysmem_VmoBuffer& source,
                                       vmo_buffer_t& destination) {
  destination.vmo = source.vmo;
  destination.vmo_usable_start = source.vmo_usable_start;
}

static void vmo_buffer_banjo_from_fidl(fuchsia_sysmem::wire::VmoBuffer& source,
                                       vmo_buffer_t& destination) {
  destination.vmo = source.vmo.release();
  destination.vmo_usable_start = source.vmo_usable_start;
}

static void vmo_buffer_fidl_from_banjo(const vmo_buffer_t& source,
                                       fuchsia_sysmem_VmoBuffer& destination) {
  destination.vmo = source.vmo;
  destination.vmo_usable_start = source.vmo_usable_start;
}

static void vmo_buffer_fidl_from_banjo(const vmo_buffer_t& source,
                                       fuchsia_sysmem::wire::VmoBuffer& destination) {
  destination.vmo = zx::vmo(source.vmo);
  destination.vmo_usable_start = source.vmo_usable_start;
}

template <typename ImageFormatConstraints>
static void image_format_constraints_banjo_from_fidl(const ImageFormatConstraints& source,
                                                     image_format_constraints_t& destination) {
  static_assert(
      std::is_same_v<ImageFormatConstraints, fuchsia_sysmem::wire::ImageFormatConstraints> ||
      std::is_same_v<ImageFormatConstraints, fuchsia_sysmem_ImageFormatConstraints>);
  pixel_format_banjo_from_fidl(source.pixel_format, destination.pixel_format);
  destination.color_spaces_count = source.color_spaces_count;
  for (size_t i = 0; i < source.color_spaces_count; i++) {
    destination.color_space[i].type = static_cast<color_space_type_t>(source.color_space[i].type);
  }
  destination.min_coded_width = source.min_coded_width;
  destination.max_coded_width = source.max_coded_width;
  destination.min_coded_height = source.min_coded_height;
  destination.max_coded_height = source.max_coded_height;
  destination.min_bytes_per_row = source.min_bytes_per_row;
  destination.max_bytes_per_row = source.max_bytes_per_row;
  destination.max_coded_width_times_coded_height = source.max_coded_width_times_coded_height;
  destination.layers = source.layers;
  destination.coded_width_divisor = source.coded_width_divisor;
  destination.coded_height_divisor = source.coded_height_divisor;
  destination.bytes_per_row_divisor = source.bytes_per_row_divisor;
  destination.start_offset_divisor = source.start_offset_divisor;
  destination.display_width_divisor = source.display_width_divisor;
  destination.display_height_divisor = source.display_height_divisor;
  destination.required_min_coded_width = source.required_min_coded_width;
  destination.required_max_coded_width = source.required_max_coded_width;
  destination.required_min_coded_height = source.required_min_coded_height;
  destination.required_max_coded_height = source.required_max_coded_height;
  destination.required_min_bytes_per_row = source.required_min_bytes_per_row;
  destination.required_max_bytes_per_row = source.required_max_bytes_per_row;
}

template <typename ImageFormatConstraints>
static void image_format_constraints_fidl_from_banjo(const image_format_constraints_t& source,
                                                     ImageFormatConstraints& destination) {
  static_assert(
      std::is_same_v<ImageFormatConstraints, fuchsia_sysmem::wire::ImageFormatConstraints> ||
      std::is_same_v<ImageFormatConstraints, fuchsia_sysmem_ImageFormatConstraints>);
  using ColorSpaceType = decltype(destination.color_space[0].type);
  pixel_format_fidl_from_banjo(source.pixel_format, destination.pixel_format);
  destination.color_spaces_count = source.color_spaces_count;
  for (size_t i = 0; i < source.color_spaces_count; i++) {
    destination.color_space[i].type = ColorSpaceType(source.color_space[i].type);
  }
  destination.min_coded_width = source.min_coded_width;
  destination.max_coded_width = source.max_coded_width;
  destination.min_coded_height = source.min_coded_height;
  destination.max_coded_height = source.max_coded_height;
  destination.min_bytes_per_row = source.min_bytes_per_row;
  destination.max_bytes_per_row = source.max_bytes_per_row;
  destination.max_coded_width_times_coded_height = source.max_coded_width_times_coded_height;
  destination.layers = source.layers;
  destination.coded_width_divisor = source.coded_width_divisor;
  destination.coded_height_divisor = source.coded_height_divisor;
  destination.bytes_per_row_divisor = source.bytes_per_row_divisor;
  destination.start_offset_divisor = source.start_offset_divisor;
  destination.display_width_divisor = source.display_width_divisor;
  destination.display_height_divisor = source.display_height_divisor;
  destination.required_min_coded_width = source.required_min_coded_width;
  destination.required_max_coded_width = source.required_max_coded_width;
  destination.required_min_coded_height = source.required_min_coded_height;
  destination.required_max_coded_height = source.required_max_coded_height;
  destination.required_min_bytes_per_row = source.required_min_bytes_per_row;
  destination.required_max_bytes_per_row = source.required_max_bytes_per_row;
}

template <typename SingleBufferSettings>
static void single_buffer_settings_banjo_from_fidl(const SingleBufferSettings& source,
                                                   single_buffer_settings_t& destination) {
  static_assert(std::is_same_v<SingleBufferSettings, fuchsia_sysmem::wire::SingleBufferSettings> ||
                std::is_same_v<SingleBufferSettings, fuchsia_sysmem_SingleBufferSettings>);
  destination.buffer_settings = {
      .size_bytes = source.buffer_settings.size_bytes,
      .is_physically_contiguous = source.buffer_settings.is_physically_contiguous,
      .is_secure = source.buffer_settings.is_secure,
      .coherency_domain = static_cast<coherency_domain_t>(source.buffer_settings.coherency_domain),
      .heap = static_cast<heap_type_t>(source.buffer_settings.heap),
  };
  destination.has_image_format_constraints = source.has_image_format_constraints;
  if (source.has_image_format_constraints) {
    image_format_constraints_banjo_from_fidl(source.image_format_constraints,
                                             destination.image_format_constraints);
  }
}

template <typename SingleBufferSettings>
static void single_buffer_settings_fidl_from_banjo(const single_buffer_settings_t& source,
                                                   SingleBufferSettings& destination) {
  static_assert(std::is_same_v<SingleBufferSettings, fuchsia_sysmem::wire::SingleBufferSettings> ||
                std::is_same_v<SingleBufferSettings, fuchsia_sysmem_SingleBufferSettings>);
  using CoherencyDomain = decltype(destination.buffer_settings.coherency_domain);
  using HeapType = decltype(destination.buffer_settings.heap);
  destination.buffer_settings = {
      .size_bytes = source.buffer_settings.size_bytes,
      .is_physically_contiguous = source.buffer_settings.is_physically_contiguous,
      .is_secure = source.buffer_settings.is_secure,
      .coherency_domain = CoherencyDomain(source.buffer_settings.coherency_domain),
      .heap = HeapType(source.buffer_settings.heap),
  };
  destination.has_image_format_constraints = source.has_image_format_constraints;
  if (source.has_image_format_constraints) {
    image_format_constraints_fidl_from_banjo(source.image_format_constraints,
                                             destination.image_format_constraints);
  }
}

void image_format_2_banjo_from_fidl(const fuchsia_sysmem_ImageFormat_2& source,
                                    image_format_2_t& destination) {
  pixel_format_banjo_from_fidl(source.pixel_format, destination.pixel_format);
  destination.coded_width = source.coded_width;
  destination.coded_height = source.coded_height;
  destination.bytes_per_row = source.bytes_per_row;
  destination.display_width = source.display_width;
  destination.display_height = source.display_height;
  destination.layers = source.layers;
  destination.color_space = {source.color_space.type};
  destination.has_pixel_aspect_ratio = source.has_pixel_aspect_ratio;
  destination.pixel_aspect_ratio_width = source.pixel_aspect_ratio_width;
  destination.pixel_aspect_ratio_height = source.pixel_aspect_ratio_height;
}

void image_format_2_banjo_from_fidl(const fuchsia_sysmem::wire::ImageFormat2& source,
                                    image_format_2_t& destination) {
  pixel_format_banjo_from_fidl(source.pixel_format, destination.pixel_format);
  destination.coded_width = source.coded_width;
  destination.coded_height = source.coded_height;
  destination.bytes_per_row = source.bytes_per_row;
  destination.display_width = source.display_width;
  destination.display_height = source.display_height;
  destination.layers = source.layers;
  destination.color_space = {static_cast<color_space_type_t>(source.color_space.type)};
  destination.has_pixel_aspect_ratio = source.has_pixel_aspect_ratio;
  destination.pixel_aspect_ratio_width = source.pixel_aspect_ratio_width;
  destination.pixel_aspect_ratio_height = source.pixel_aspect_ratio_height;
}

void image_format_2_fidl_from_banjo(const image_format_2_t& source,
                                    fuchsia_sysmem_ImageFormat_2& destination) {
  pixel_format_fidl_from_banjo(source.pixel_format, destination.pixel_format);
  destination.coded_width = source.coded_width;
  destination.coded_height = source.coded_height;
  destination.bytes_per_row = source.bytes_per_row;
  destination.display_width = source.display_width;
  destination.display_height = source.display_height;
  destination.layers = source.layers;
  destination.color_space = {source.color_space.type};
  destination.has_pixel_aspect_ratio = source.has_pixel_aspect_ratio;
  destination.pixel_aspect_ratio_width = source.pixel_aspect_ratio_width;
  destination.pixel_aspect_ratio_height = source.pixel_aspect_ratio_height;
}

void image_format_2_fidl_from_banjo(const image_format_2_t& source,
                                    fuchsia_sysmem::wire::ImageFormat2& destination) {
  pixel_format_fidl_from_banjo(source.pixel_format, destination.pixel_format);
  destination.coded_width = source.coded_width;
  destination.coded_height = source.coded_height;
  destination.bytes_per_row = source.bytes_per_row;
  destination.display_width = source.display_width;
  destination.display_height = source.display_height;
  destination.layers = source.layers;
  destination.color_space = {fuchsia_sysmem::wire::ColorSpaceType(source.color_space.type)};
  destination.has_pixel_aspect_ratio = source.has_pixel_aspect_ratio;
  destination.pixel_aspect_ratio_width = source.pixel_aspect_ratio_width;
  destination.pixel_aspect_ratio_height = source.pixel_aspect_ratio_height;
}

void buffer_collection_info_2_banjo_from_fidl(const fuchsia_sysmem_BufferCollectionInfo_2& source,
                                              buffer_collection_info_2_t& destination) {
  destination.buffer_count = source.buffer_count;
  single_buffer_settings_banjo_from_fidl(source.settings, destination.settings);
  for (size_t i = 0; i < source.buffer_count; i++) {
    vmo_buffer_banjo_from_fidl(source.buffers[i], destination.buffers[i]);
  }
}

void buffer_collection_info_2_banjo_from_fidl(fuchsia_sysmem::wire::BufferCollectionInfo2&& source,
                                              buffer_collection_info_2_t& destination) {
  destination.buffer_count = source.buffer_count;
  single_buffer_settings_banjo_from_fidl(source.settings, destination.settings);
  for (size_t i = 0; i < source.buffer_count; i++) {
    vmo_buffer_banjo_from_fidl(source.buffers[i], destination.buffers[i]);
  }
}

void buffer_collection_info_2_fidl_from_banjo(const buffer_collection_info_2_t& source,
                                              fuchsia_sysmem_BufferCollectionInfo_2& destination) {
  destination.buffer_count = source.buffer_count;
  single_buffer_settings_fidl_from_banjo(source.settings, destination.settings);
  for (size_t i = 0; i < source.buffer_count; i++) {
    vmo_buffer_fidl_from_banjo(source.buffers[i], destination.buffers[i]);
  }
}

void buffer_collection_info_2_fidl_from_banjo(
    const buffer_collection_info_2_t& source,
    fuchsia_sysmem::wire::BufferCollectionInfo2& destination) {
  destination.buffer_count = source.buffer_count;
  single_buffer_settings_fidl_from_banjo(source.settings, destination.settings);
  for (size_t i = 0; i < source.buffer_count; i++) {
    vmo_buffer_fidl_from_banjo(source.buffers[i], destination.buffers[i]);
  }
}

}  // namespace sysmem
