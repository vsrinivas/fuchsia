// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/formatting.h"

#include "src/lib/fostr/indent.h"

namespace media_player {
namespace test {

#define FORMAT_MEMBER(os, value, member, def)                               \
  if (value.member != def) {                                                \
    os << fostr::NewLine << "." << #member << " = " << value.member << ","; \
  }

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::BufferUsage& value) {
  os << "{" << fostr::Indent;
  if (value.none) {
    os << fostr::NewLine << ".none = fuchsia::sysmem::noneUsage,";
  }

  if (value.cpu) {
    os << fostr::NewLine << ".cpu = ";
    bool wrote_one = false;
    if (value.cpu & fuchsia::sysmem::cpuUsageRead) {
      os << "fuchsia::sysmem::cpuUsageRead";
      wrote_one = true;
    }
    if (value.cpu & fuchsia::sysmem::cpuUsageReadOften) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::cpuUsageReadOften";
      wrote_one = true;
    }
    if (value.cpu & fuchsia::sysmem::cpuUsageWrite) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::cpuUsageWrite";
      wrote_one = true;
    }
    if (value.cpu & fuchsia::sysmem::cpuUsageWriteOften) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::cpuUsageWriteOften";
      wrote_one = true;
    }
    os << ",";
  }

  if (value.vulkan) {
    os << fostr::NewLine << ".vulkan = ";
    bool wrote_one = false;
    if (value.vulkan & fuchsia::sysmem::vulkanUsageTransferSrc) {
      os << "fuchsia::sysmem::vulkanUsageTransferSrc";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageTransferDst) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageTransferDst";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageSampled) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageSampled";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageStorage) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageStorage";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageColorAttachment) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageColorAttachment";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageStencilAttachment) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageStencilAttachment";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageTransientAttachment) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageTransientAttachment";
    }
    if (value.vulkan & fuchsia::sysmem::vulkanUsageInputAttachment) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::vulkanUsageInputAttachment";
    }
    os << ",";
  }

  if (value.display) {
    os << fostr::NewLine << ".display = ";
    bool wrote_one = false;
    if (value.display & fuchsia::sysmem::displayUsageLayer) {
      os << "fuchsia::sysmem::displayUsageLayer";
    }
    if (value.display & fuchsia::sysmem::displayUsageCursor) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::displayUsageCursor";
    }
    os << ",";
  }

  if (value.video) {
    os << fostr::NewLine << ".video = ";
    bool wrote_one = false;
    if (value.video & fuchsia::sysmem::videoUsageHwDecoder) {
      os << "fuchsia::sysmem::videoUsageHwDecoder";
    }
    if (value.video & fuchsia::sysmem::videoUsageHwEncoder) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::videoUsageHwEncoder";
    }
    if (value.video & fuchsia::sysmem::videoUsageHwProtected) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::videoUsageHwProtected";
    }
    if (value.video & fuchsia::sysmem::videoUsageCapture) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::videoUsageCapture";
    }
    if (value.video & fuchsia::sysmem::videoUsageDecryptorOutput) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::videoUsageDecryptorOutput";
    }
    if (value.video & fuchsia::sysmem::videoUsageHwDecoderInternal) {
      os << (wrote_one ? "|" : "") << "fuchsia::sysmem::videoUsageHwDecoderInternal";
    }
    os << ",";
  }

  return os << fostr::Outdent << fostr::NewLine << "}";
}

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::HeapType value) {
  return os << "// TODO: Write HeapType formatter.";
}

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::BufferMemoryConstraints& value) {
  os << "{" << fostr::Indent;
  FORMAT_MEMBER(os, value, min_size_bytes, 0);
  FORMAT_MEMBER(os, value, max_size_bytes, 0xFFFFFFFF);
  if (value.physically_contiguous_required) {
    os << fostr::NewLine << ".physically_contiguous_required = true,";
  }
  if (value.secure_required) {
    os << fostr::NewLine << ".secure_required = true,";
  }
  if (value.ram_domain_supported) {
    os << fostr::NewLine << ".ram_domain_supported = true,";
  }
  if (!value.cpu_domain_supported) {
    os << fostr::NewLine << ".cpu_domain_supported = false,";
  }
  if (value.inaccessible_domain_supported) {
    os << fostr::NewLine << ".inaccessible_domain_supported = true,";
  }
  if (value.heap_permitted_count != 0) {
    os << fostr::NewLine << ".heap_permitted_count = " << value.heap_permitted_count << ",";
    os << fostr::NewLine << ".heap_permitted = {";
    for (size_t i = 0; i < value.heap_permitted_count; ++i) {
      os << fostr::NewLine << value.heap_permitted[i] << ",";
    }
    os << fostr::NewLine << "},";
  }
  return os << fostr::Outdent << fostr::NewLine << "}";
}

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::PixelFormatType value) {
  switch (value) {
    case fuchsia::sysmem::PixelFormatType::INVALID:
      return os << "fuchsia::sysmem::PixelFormatType::INVALID";
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return os << "fuchsia::sysmem::PixelFormatType::R8G8B8A8";
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return os << "fuchsia::sysmem::PixelFormatType::BGRA32";
    case fuchsia::sysmem::PixelFormatType::I420:
      return os << "fuchsia::sysmem::PixelFormatType::I420";
    case fuchsia::sysmem::PixelFormatType::M420:
      return os << "fuchsia::sysmem::PixelFormatType::M420";
    case fuchsia::sysmem::PixelFormatType::NV12:
      return os << "fuchsia::sysmem::PixelFormatType::NV12";
    case fuchsia::sysmem::PixelFormatType::YUY2:
      return os << "fuchsia::sysmem::PixelFormatType::YUY2";
    case fuchsia::sysmem::PixelFormatType::MJPEG:
      return os << "fuchsia::sysmem::PixelFormatType::MJPEG";
    case fuchsia::sysmem::PixelFormatType::YV12:
      return os << "fuchsia::sysmem::PixelFormatType::YV12";
    case fuchsia::sysmem::PixelFormatType::BGR24:
      return os << "fuchsia::sysmem::PixelFormatType::BGR24";
    case fuchsia::sysmem::PixelFormatType::RGB565:
      return os << "fuchsia::sysmem::PixelFormatType::RGB565";
    case fuchsia::sysmem::PixelFormatType::RGB332:
      return os << "fuchsia::sysmem::PixelFormatType::RGB332";
    case fuchsia::sysmem::PixelFormatType::RGB2220:
      return os << "fuchsia::sysmem::PixelFormatType::RGB2220";
    case fuchsia::sysmem::PixelFormatType::L8:
      return os << "fuchsia::sysmem::PixelFormatType::L8";
    case fuchsia::sysmem::PixelFormatType::R8:
      return os << "fuchsia::sysmem::PixelFormatType::R8";
    case fuchsia::sysmem::PixelFormatType::R8G8:
      return os << "fuchsia::sysmem::PixelFormatType::R8G8";
    default:
      return os << "Unknown format";
  }
}

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::FormatModifier value) {
  return os << "// TODO: Write FormatModifier formatter.";
}

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::PixelFormat& value) {
  if (value.has_format_modifier) {
    os << "{" << fostr::Indent;
    os << fostr::NewLine << ".type = " << value.type << ",";
    os << fostr::NewLine << ".has_format_modifier = true,";
    os << fostr::NewLine << ".format_modifier = " << value.format_modifier;
    return os << fostr::Outdent << fostr::NewLine << "}";
  } else {
    return os << "{.type = " << value.type << "}";
  }
}

std::ostream& operator<<(std::ostream& os, fuchsia::sysmem::ColorSpaceType value) {
  switch (value) {
    case fuchsia::sysmem::ColorSpaceType::INVALID:
      return os << "fuchsia::sysmem::ColorSpaceType::INVALID";
    case fuchsia::sysmem::ColorSpaceType::SRGB:
      return os << "fuchsia::sysmem::ColorSpaceType::SRGB";
    case fuchsia::sysmem::ColorSpaceType::REC601_NTSC:
      return os << "fuchsia::sysmem::ColorSpaceType::REC601_NTSC";
    case fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE:
      return os << "fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE";
    case fuchsia::sysmem::ColorSpaceType::REC601_PAL:
      return os << "fuchsia::sysmem::ColorSpaceType::REC601_PAL";
    case fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE:
      return os << "fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE";
    case fuchsia::sysmem::ColorSpaceType::REC709:
      return os << "fuchsia::sysmem::ColorSpaceType::REC709";
    case fuchsia::sysmem::ColorSpaceType::REC2020:
      return os << "fuchsia::sysmem::ColorSpaceType::REC2020";
    case fuchsia::sysmem::ColorSpaceType::REC2100:
      return os << "fuchsia::sysmem::ColorSpaceType::REC2100";
    case fuchsia::sysmem::ColorSpaceType::PASS_THROUGH:
      return os << "fuchsia::sysmem::ColorSpaceType::PASS_THROUGH";
    case fuchsia::sysmem::ColorSpaceType::DO_NOT_CARE:
      return os << "fuchsia::sysmem::ColorSpaceType::DO_NOT_CARE";
  }
}

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ColorSpace& value) {
  return os << "{.type = " << value.type << "}";
}

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ImageFormatConstraints& value) {
  os << "{" << fostr::Indent;
  os << fostr::NewLine << ".pixel_format = " << value.pixel_format << ",";
  if (value.color_spaces_count != 0) {
    os << fostr::NewLine << ".color_spaces_count = " << value.color_spaces_count << ",";
    os << fostr::NewLine << ".color_space = {" << fostr::Indent;
    for (size_t i = 0; i < value.color_spaces_count; ++i) {
      os << fostr::NewLine << "fuchsia::sysmem::ColorSpace" << value.color_space[i] << ",";
    }
    os << fostr::Outdent << fostr::NewLine << "},";
  }
  FORMAT_MEMBER(os, value, min_coded_width, 0);
  FORMAT_MEMBER(os, value, max_coded_width, 0);
  FORMAT_MEMBER(os, value, min_coded_height, 0);
  FORMAT_MEMBER(os, value, max_coded_height, 0);
  FORMAT_MEMBER(os, value, min_bytes_per_row, 0);
  FORMAT_MEMBER(os, value, max_bytes_per_row, 0);
  FORMAT_MEMBER(os, value, max_coded_width_times_coded_height, 0xFFFFFFFF);
  FORMAT_MEMBER(os, value, layers, 1);
  FORMAT_MEMBER(os, value, coded_width_divisor, 1);
  FORMAT_MEMBER(os, value, coded_height_divisor, 1);
  FORMAT_MEMBER(os, value, bytes_per_row_divisor, 1);
  FORMAT_MEMBER(os, value, start_offset_divisor, 1);
  FORMAT_MEMBER(os, value, display_width_divisor, 1);
  FORMAT_MEMBER(os, value, display_height_divisor, 1);
  FORMAT_MEMBER(os, value, required_min_coded_width, 0);
  FORMAT_MEMBER(os, value, required_max_coded_width, 0);
  FORMAT_MEMBER(os, value, required_min_coded_height, 0);
  FORMAT_MEMBER(os, value, required_max_coded_height, 0);
  FORMAT_MEMBER(os, value, required_min_bytes_per_row, 0);
  FORMAT_MEMBER(os, value, required_max_bytes_per_row, 0);
  return os << fostr::Outdent << fostr::NewLine << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::sysmem::BufferCollectionConstraints& value) {
  os << "{" << fostr::Indent;
  os << fostr::NewLine << ".usage = " << value.usage << ",";
  FORMAT_MEMBER(os, value, min_buffer_count_for_camping, 0);
  FORMAT_MEMBER(os, value, min_buffer_count_for_dedicated_slack, 0);
  FORMAT_MEMBER(os, value, min_buffer_count_for_shared_slack, 0);
  FORMAT_MEMBER(os, value, min_buffer_count, 0);
  FORMAT_MEMBER(os, value, max_buffer_count, 0);
  if (value.has_buffer_memory_constraints) {
    os << fostr::NewLine << ".has_buffer_memory_constraints = true,";
    os << fostr::NewLine << ".buffer_memory_constraints = " << value.buffer_memory_constraints
       << ",";
  }
  if (value.image_format_constraints_count != 0) {
    os << fostr::NewLine
       << ".image_format_constraints_count = " << value.image_format_constraints_count << ",";
    os << fostr::NewLine << ".image_format_constraints = {" << fostr::Indent;
    for (size_t i = 0; i < value.image_format_constraints_count; ++i) {
      os << fostr::NewLine << "fuchsia::sysmem::ImageFormatConstraints"
         << value.image_format_constraints[i] << ",";
    }
    os << fostr::NewLine << "}," << fostr::Outdent;
  }

  return os << fostr::Outdent << fostr::NewLine << "}";
}

std::ostream& operator<<(std::ostream& os, const fuchsia::sysmem::ImageFormat_2& value) {
  os << "{" << fostr::Indent;
  os << fostr::NewLine << ".pixel_format = " << value.pixel_format << ",";
  FORMAT_MEMBER(os, value, coded_width, 0);
  FORMAT_MEMBER(os, value, coded_height, 0);
  FORMAT_MEMBER(os, value, bytes_per_row, 0);
  FORMAT_MEMBER(os, value, display_width, 0);
  FORMAT_MEMBER(os, value, display_height, 0);
  os << fostr::NewLine << ".color_space = " << value.color_space << "," << std::boolalpha;
  FORMAT_MEMBER(os, value, has_pixel_aspect_ratio, false);
  FORMAT_MEMBER(os, value, pixel_aspect_ratio_width, 1);
  FORMAT_MEMBER(os, value, pixel_aspect_ratio_height, 1);
  return os << fostr::Outdent << fostr::NewLine << "}";
}

}  // namespace test
}  // namespace media_player
