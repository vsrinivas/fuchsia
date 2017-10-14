// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/vulkan_utils.h"

namespace escher {
namespace impl {

std::vector<vk::Format> GetSupportedDepthFormats(
    vk::PhysicalDevice device,
    std::vector<vk::Format> desired_formats) {
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

uint32_t GetMemoryTypeIndex(vk::PhysicalDevice device,
                            uint32_t type_bits,
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
  FXL_CHECK(false);
  return 0;
}

// Return the sample-count corresponding to the specified flag-bits.
uint32_t SampleCountFlagBitsToInt(vk::SampleCountFlagBits bits) {
  switch (bits) {
    case vk::SampleCountFlagBits::e1:
      return 1;
    case vk::SampleCountFlagBits::e2:
      return 2;
    case vk::SampleCountFlagBits::e4:
      return 4;
    case vk::SampleCountFlagBits::e8:
      return 8;
    case vk::SampleCountFlagBits::e16:
      return 16;
    case vk::SampleCountFlagBits::e32:
      return 32;
    case vk::SampleCountFlagBits::e64:
      return 64;
  }
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

}  // namespace impl
}  // namespace escher
