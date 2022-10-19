// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/formatting/formatting.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "src/camera/lib/formatting/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera::formatting {

struct PropertyList;
using PropertyListPtr = std::unique_ptr<PropertyList>;
struct Property {
  std::optional<std::string> name;
  std::variant<std::string, PropertyListPtr> value;
};
struct PropertyList {
  std::vector<Property> properties;
  static PropertyListPtr New() { return std::make_unique<PropertyList>(); }
  // NOLINTNEXTLINE(misc-no-recursion): naturally recursive type
  void Format(std::ostream& os, size_t depth = 0) const {
    std::string pad(depth * 2, ' ');
    for (const auto& property : properties) {
      if (property.name) {
        os << pad << "\"" << *property.name << "\": ";
      }
      if (property.value.index() == 0) {
        os << "\"" << std::get<std::string>(property.value) << "\"\n";
      } else {
        os << "{\n";
        std::get<1>(property.value)->Format(os, depth + 1);
        os << "}\n";
      }
    }
  }
};

std::ostream& operator<<(std::ostream& os, const PropertyList& p) {
  p.Format(os);
  return os;
}

std::ostream& operator<<(std::ostream& os, const PropertyListPtr& p) {
  if (p) {
    os << *p;
  } else {
    os << "<MISSING PROPERTY LIST>";
  }
  return os;
}

template <typename T>
PropertyListPtr Dump(const std::vector<T>& x) {
  auto p = PropertyList::New();
  for (size_t i = 0; i < std::size(x); ++i) {
    p->properties.push_back({.name = std::to_string(i), .value = Dump(x[i])});
  }
  return p;
}

template <typename T, size_t N>
PropertyListPtr Dump(const std::array<T, N>& x) {
  auto p = PropertyList::New();
  for (size_t i = 0; i < std::size(x); ++i) {
    p->properties.push_back({.name = std::to_string(i), .value = Dump(x[i])});
  }
  return p;
}

template <typename T>
PropertyListPtr Dump(const T& x) {
  auto p = PropertyList::New();
  p->properties.push_back({.value = std::to_string(x)});
  return p;
}

template <>
PropertyListPtr Dump(const std::string& x) {
  auto p = PropertyList::New();
  p->properties.push_back({.value = x});
  return p;
}

template <>
PropertyListPtr Dump(const bool& x) {
  auto p = PropertyList::New();
  p->properties.push_back({.value = (x ? "true" : "false")});
  return p;
}

template <>
PropertyListPtr Dump(const zx::vmo& x) {
  auto p = PropertyList::New();
  p->properties.push_back({.name = "koid", .value = std::to_string(fsl::GetKoid(x.get()))});
  return p;
}

}  // namespace camera::formatting

ENUM_BEGIN(fuchsia::camera2, DeviceType)
ENUM_ELEMENT(DeviceType::BUILTIN)
ENUM_ELEMENT(DeviceType::VIRTUAL)
ENUM_END()

ENUM_BEGIN(fuchsia::camera2, FrameStatus)
ENUM_ELEMENT(FrameStatus::OK)
ENUM_ELEMENT(FrameStatus::ERROR_FRAME)
ENUM_ELEMENT(FrameStatus::ERROR_BUFFER_FULL)
ENUM_END()

ENUM_BEGIN(fuchsia::sysmem, HeapType)
ENUM_ELEMENT(HeapType::SYSTEM_RAM)
ENUM_ELEMENT(HeapType::AMLOGIC_SECURE)
ENUM_ELEMENT(HeapType::AMLOGIC_SECURE_VDEC)
ENUM_ELEMENT(HeapType::GOLDFISH_DEVICE_LOCAL)
ENUM_ELEMENT(HeapType::GOLDFISH_HOST_VISIBLE)
ENUM_ELEMENT(HeapType::FRAMEBUFFER)
ENUM_END()

ENUM_BEGIN(fuchsia::sysmem, CoherencyDomain)
ENUM_ELEMENT(CoherencyDomain::CPU)
ENUM_ELEMENT(CoherencyDomain::RAM)
ENUM_ELEMENT(CoherencyDomain::INACCESSIBLE)
ENUM_END()

ENUM_BEGIN(fuchsia::sysmem, PixelFormatType)
ENUM_ELEMENT(PixelFormatType::INVALID)
ENUM_ELEMENT(PixelFormatType::R8G8B8A8)
ENUM_ELEMENT(PixelFormatType::BGRA32)
ENUM_ELEMENT(PixelFormatType::I420)
ENUM_ELEMENT(PixelFormatType::M420)
ENUM_ELEMENT(PixelFormatType::NV12)
ENUM_ELEMENT(PixelFormatType::YUY2)
ENUM_ELEMENT(PixelFormatType::MJPEG)
ENUM_ELEMENT(PixelFormatType::YV12)
ENUM_ELEMENT(PixelFormatType::BGR24)
ENUM_ELEMENT(PixelFormatType::RGB565)
ENUM_ELEMENT(PixelFormatType::RGB332)
ENUM_ELEMENT(PixelFormatType::RGB2220)
ENUM_ELEMENT(PixelFormatType::L8)
ENUM_ELEMENT(PixelFormatType::R8)
ENUM_ELEMENT(PixelFormatType::R8G8)
ENUM_ELEMENT(PixelFormatType::A2R10G10B10)
ENUM_ELEMENT(PixelFormatType::A2B10G10R10)
ENUM_ELEMENT(PixelFormatType::DO_NOT_CARE)
ENUM_END()

ENUM_BEGIN(fuchsia::sysmem, ColorSpaceType)
ENUM_ELEMENT(ColorSpaceType::INVALID)
ENUM_ELEMENT(ColorSpaceType::SRGB)
ENUM_ELEMENT(ColorSpaceType::REC601_NTSC)
ENUM_ELEMENT(ColorSpaceType::REC601_NTSC_FULL_RANGE)
ENUM_ELEMENT(ColorSpaceType::REC601_PAL)
ENUM_ELEMENT(ColorSpaceType::REC601_PAL_FULL_RANGE)
ENUM_ELEMENT(ColorSpaceType::REC709)
ENUM_ELEMENT(ColorSpaceType::REC2020)
ENUM_ELEMENT(ColorSpaceType::REC2100)
ENUM_ELEMENT(ColorSpaceType::PASS_THROUGH)
ENUM_ELEMENT(ColorSpaceType::DO_NOT_CARE)
ENUM_END()

BITS_BEGIN(fuchsia::camera2, CameraStreamType)
BITS_ELEMENT(CameraStreamType::MACHINE_LEARNING)
BITS_ELEMENT(CameraStreamType::MONITORING)
BITS_ELEMENT(CameraStreamType::FULL_RESOLUTION)
BITS_ELEMENT(CameraStreamType::DOWNSCALED_RESOLUTION)
BITS_ELEMENT(CameraStreamType::VIDEO_CONFERENCE)
BITS_ELEMENT(CameraStreamType::EXTENDED_FOV)
BITS_END()

MARK_NOTIMPL(fuchsia::sysmem, FormatModifier)

STRUCT_BEGIN(fuchsia::sysmem, BufferUsage)
STRUCT_ELEMENT(none)
STRUCT_ELEMENT(cpu)
STRUCT_ELEMENT(vulkan)
STRUCT_ELEMENT(display)
STRUCT_ELEMENT(video)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, PixelFormat)
STRUCT_ELEMENT(type)
STRUCT_ELEMENT(has_format_modifier)
STRUCT_ELEMENT(format_modifier)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, BufferMemorySettings)
STRUCT_ELEMENT(size_bytes)
STRUCT_ELEMENT(is_physically_contiguous)
STRUCT_ELEMENT(is_secure)
STRUCT_ELEMENT(coherency_domain)
STRUCT_ELEMENT(heap)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, ColorSpace)
STRUCT_ELEMENT(type)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, ImageFormatConstraints)
STRUCT_ELEMENT(pixel_format)
STRUCT_ELEMENT(color_spaces_count)
STRUCT_ELEMENT(color_space)
STRUCT_ELEMENT(min_coded_width)
STRUCT_ELEMENT(max_coded_width)
STRUCT_ELEMENT(min_coded_height)
STRUCT_ELEMENT(max_coded_height)
STRUCT_ELEMENT(max_bytes_per_row)
STRUCT_ELEMENT(max_coded_width_times_coded_height)
STRUCT_ELEMENT(layers)
STRUCT_ELEMENT(coded_width_divisor)
STRUCT_ELEMENT(coded_height_divisor)
STRUCT_ELEMENT(bytes_per_row_divisor)
STRUCT_ELEMENT(start_offset_divisor)
STRUCT_ELEMENT(display_width_divisor)
STRUCT_ELEMENT(display_height_divisor)
STRUCT_ELEMENT(required_min_coded_width)
STRUCT_ELEMENT(required_max_coded_width)
STRUCT_ELEMENT(required_min_coded_height)
STRUCT_ELEMENT(required_max_coded_height)
STRUCT_ELEMENT(required_min_bytes_per_row)
STRUCT_ELEMENT(required_max_bytes_per_row)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, ImageFormat_2)
STRUCT_ELEMENT(pixel_format)
STRUCT_ELEMENT(coded_width)
STRUCT_ELEMENT(coded_height)
STRUCT_ELEMENT(bytes_per_row)
STRUCT_ELEMENT(display_width)
STRUCT_ELEMENT(display_height)
STRUCT_ELEMENT(layers)
STRUCT_ELEMENT(color_space)
STRUCT_ELEMENT(has_pixel_aspect_ratio)
STRUCT_ELEMENT(pixel_aspect_ratio_width)
STRUCT_ELEMENT(pixel_aspect_ratio_height)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, BufferMemoryConstraints)
STRUCT_ELEMENT(min_size_bytes)
STRUCT_ELEMENT(max_size_bytes)
STRUCT_ELEMENT(physically_contiguous_required)
STRUCT_ELEMENT(secure_required)
STRUCT_ELEMENT(ram_domain_supported)
STRUCT_ELEMENT(cpu_domain_supported)
STRUCT_ELEMENT(inaccessible_domain_supported)
STRUCT_ELEMENT(heap_permitted_count)
STRUCT_ELEMENT(heap_permitted)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, VmoBuffer)
STRUCT_ELEMENT(vmo)
STRUCT_ELEMENT(vmo_usable_start)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, SingleBufferSettings)
STRUCT_ELEMENT(buffer_settings)
STRUCT_ELEMENT(has_image_format_constraints)
STRUCT_ELEMENT(image_format_constraints)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, BufferCollectionConstraints)
STRUCT_ELEMENT(usage)
STRUCT_ELEMENT(min_buffer_count_for_camping)
STRUCT_ELEMENT(min_buffer_count_for_dedicated_slack)
STRUCT_ELEMENT(min_buffer_count_for_shared_slack)
STRUCT_ELEMENT(min_buffer_count)
STRUCT_ELEMENT(max_buffer_count)
STRUCT_ELEMENT(has_buffer_memory_constraints)
STRUCT_ELEMENT(buffer_memory_constraints)
STRUCT_ELEMENT(image_format_constraints_count)
STRUCT_ELEMENT(image_format_constraints)
STRUCT_END()

STRUCT_BEGIN(fuchsia::sysmem, BufferCollectionInfo_2)
STRUCT_ELEMENT(buffer_count)
STRUCT_ELEMENT(settings)
STRUCT_ELEMENT(buffers)
STRUCT_END()

STRUCT_BEGIN(fuchsia::camera2, FrameRate)
STRUCT_ELEMENT(frames_per_sec_numerator)
STRUCT_ELEMENT(frames_per_sec_denominator)
STRUCT_END()

TABLE_BEGIN(fuchsia::camera2, FrameMetadata)
TABLE_ELEMENT(timestamp);
TABLE_ELEMENT(image_format_index);
TABLE_ELEMENT(capture_timestamp);
TABLE_END()

TABLE_BEGIN(fuchsia::camera2, DeviceInfo)
TABLE_ELEMENT(vendor_id)
TABLE_ELEMENT(vendor_name)
TABLE_ELEMENT(product_id)
TABLE_ELEMENT(product_name)
TABLE_ELEMENT(type)
TABLE_END()

TABLE_BEGIN(fuchsia::camera2, StreamProperties)
TABLE_ELEMENT(stream_type)
TABLE_END()

STRUCT_BEGIN(fuchsia::camera2, FrameAvailableInfo)
STRUCT_ELEMENT(frame_status)
STRUCT_ELEMENT(buffer_id)
STRUCT_ELEMENT(metadata)
STRUCT_END()

TABLE_BEGIN(fuchsia::camera2, StreamConstraints)
TABLE_ELEMENT(properties)
TABLE_ELEMENT(format_index)
TABLE_END()

STRUCT_BEGIN(fuchsia::camera2::hal, Config)
STRUCT_ELEMENT(stream_configs)
STRUCT_END()

STRUCT_BEGIN(fuchsia::camera2::hal, StreamConfig)
STRUCT_ELEMENT(frame_rate)
STRUCT_ELEMENT(constraints)
STRUCT_ELEMENT(properties)
STRUCT_ELEMENT(image_formats)
STRUCT_END()
