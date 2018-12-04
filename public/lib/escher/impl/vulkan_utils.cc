// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

std::vector<vk::Format> GetSupportedDepthFormats(
    vk::PhysicalDevice device, std::vector<vk::Format> desired_formats) {
  std::vector<vk::Format> result;
  for (auto& fmt : desired_formats) {
    vk::FormatProperties props = device.getFormatProperties(fmt);
    if (props.optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
      result.push_back(fmt);
    }
  }
  return result;
}

FormatResult GetSupportedDepthFormat(vk::PhysicalDevice device) {
  auto supported_formats = GetSupportedDepthFormats(
      device, {vk::Format::eD16Unorm, vk::Format::eD32Sfloat});
  if (supported_formats.empty()) {
    auto undefined = vk::Format::eUndefined;
    return FormatResult(vk::Result::eErrorFeatureNotPresent, undefined);
  } else {
    return FormatResult(vk::Result::eSuccess, supported_formats[0]);
  }
}

FormatResult GetSupportedDepthStencilFormat(vk::PhysicalDevice device) {
  auto supported_formats = GetSupportedDepthFormats(
      device, {vk::Format::eD16UnormS8Uint, vk::Format::eD24UnormS8Uint,
               vk::Format::eD32SfloatS8Uint});
  if (supported_formats.empty()) {
    auto undefined = vk::Format::eUndefined;
    return FormatResult(vk::Result::eErrorFeatureNotPresent, undefined);
  } else {
    return FormatResult(vk::Result::eSuccess, supported_formats[0]);
  }
}

uint32_t GetMemoryTypeIndex(vk::PhysicalDevice device, uint32_t type_bits,
                            vk::MemoryPropertyFlags required_properties) {
  vk::PhysicalDeviceMemoryProperties memory_types =
      device.getMemoryProperties();
  for (uint32_t i = 0; i < memory_types.memoryTypeCount; ++i) {
    if ((type_bits & 1) == 1) {
      auto available_properties = memory_types.memoryTypes[i].propertyFlags;
      if ((available_properties & required_properties) == required_properties)
        return i;
    }
    type_bits >>= 1;
  }
  FXL_CHECK(false) << "Could not find memory with properties "
                   << (VkMemoryPropertyFlags)required_properties;
  return 0;
}

// Return the sample-count corresponding to the specified flag-bits.
uint32_t SampleCountFlagBitsToInt(vk::SampleCountFlagBits bits) {
  static_assert(VK_SAMPLE_COUNT_1_BIT == 1 && VK_SAMPLE_COUNT_2_BIT == 2 &&
                    VK_SAMPLE_COUNT_4_BIT == 4 && VK_SAMPLE_COUNT_8_BIT == 8 &&
                    VK_SAMPLE_COUNT_16_BIT == 16 &&
                    VK_SAMPLE_COUNT_32_BIT == 32 &&
                    VK_SAMPLE_COUNT_64_BIT == 64,
                "unexpected sample count values");

  return static_cast<uint32_t>(bits);
}

// Return flag-bits corresponding to the specified sample count.  Explode if
// an invalid value is provided.
vk::SampleCountFlagBits SampleCountFlagBitsFromInt(uint32_t sample_count) {
  switch (sample_count) {
    case 1:
      return vk::SampleCountFlagBits::e1;
    case 2:
      return vk::SampleCountFlagBits::e2;
    case 4:
      return vk::SampleCountFlagBits::e4;
    case 8:
      return vk::SampleCountFlagBits::e8;
    case 16:
      return vk::SampleCountFlagBits::e16;
    case 32:
      return vk::SampleCountFlagBits::e32;
    case 64:
      return vk::SampleCountFlagBits::e64;
    default:
      FXL_CHECK(false);
      return vk::SampleCountFlagBits::e1;
  }
}

void ClipToRect(vk::Rect2D* clippee, const vk::Rect2D& clipper) {
  int32_t min_x = std::max(clippee->offset.x, clipper.offset.x);
  int32_t max_x = std::min((clippee->offset.x + clippee->extent.width),
                           (clipper.offset.x + clipper.extent.width));
  int32_t min_y = std::max(clippee->offset.y, clipper.offset.y);
  int32_t max_y = std::min((clippee->offset.y + clippee->extent.height),
                           (clipper.offset.y + clipper.extent.height));

  // Detect overflow.
  FXL_DCHECK(max_x >= min_x && max_y >= min_y);

  clippee->offset.x = min_x;
  clippee->offset.y = min_y;
  clippee->extent.width = max_x - min_x;
  clippee->extent.height = max_y - min_y;
}

}  // namespace impl
}  // namespace escher
