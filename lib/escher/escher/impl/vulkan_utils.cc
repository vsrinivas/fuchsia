// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/vulkan_utils.h"

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
      device, {vk::Format::eD32SfloatS8Uint, vk::Format::eD32Sfloat,
               vk::Format::eD24UnormS8Uint, vk::Format::eD16UnormS8Uint,
               vk::Format::eD16Unorm});
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
  FTL_CHECK(false);
  return 0;
}

}  // namespace impl
}  // namespace escher
