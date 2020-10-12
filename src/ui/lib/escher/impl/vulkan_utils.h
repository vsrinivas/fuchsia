// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_VULKAN_UTILS_H_
#define SRC_UI_LIB_ESCHER_IMPL_VULKAN_UTILS_H_

#include <lib/syslog/cpp/macros.h>

#include <set>
#include <vector>

#include <vulkan/vulkan.hpp>

#if defined(ESCHER_VK_UTILS)
#error "vulkan_utils.h should not be included from header files"
#endif
#define ESCHER_VK_UTILS

// Log Vulkan error, if any.
#define ESCHER_LOG_VK_ERROR(ERR, MSG)                           \
  {                                                             \
    vk::Result error = ERR;                                     \
    const char* message = MSG;                                  \
    if (error != vk::Result::eSuccess) {                        \
      FX_LOGS(WARNING) << message << " : " << to_string(error); \
    }                                                           \
  }

namespace escher {

// Panic if operation was unsuccessful.
template <typename T>
auto ESCHER_CHECKED_VK_RESULT(typename vk::ResultValue<T> result) -> T {
  FX_CHECK(result.result == vk::Result::eSuccess);
  return result.value;
}

namespace impl {

// Check if the given vk::ImageCreateInfo is valid for the device.
bool CheckImageCreateInfoValidity(vk::PhysicalDevice device, const vk::ImageCreateInfo& info);

// Create a default vk::BufferImageCopy object for a width x height image.
vk::BufferImageCopy GetDefaultBufferImageCopy(size_t width, size_t height);

// Filter the |desired_formats| list to contain only those formats which support
// optimal tiling.
std::vector<vk::Format> GetSupportedDepthFormats(vk::PhysicalDevice device,
                                                 std::vector<vk::Format> desired_formats);

// Get all the MSAA sample counts from the vk::SampleCountFlags
std::set<size_t> GetSupportedColorSampleCounts(vk::SampleCountFlags flags);

// Pick the lowest precision depth format that supports optimal tiling.
typedef vk::ResultValueType<vk::Format>::type FormatResult;
FormatResult GetSupportedDepthFormat(vk::PhysicalDevice device);

// Pick the lowest precision depth/stencil format that supports optimal tiling.
FormatResult GetSupportedDepthStencilFormat(vk::PhysicalDevice device);

// Search through all memory types specified by |type_bits| and return the index
// of the first one that has all necessary flags.  Returns memoryTypeCount of
// |device|'s memory properties if nones is found.
uint32_t GetMemoryTypeIndex(vk::PhysicalDevice device, uint32_t type_bits,
                            vk::MemoryPropertyFlags required_properties);

// Return the sample-count corresponding to the specified flag-bits.
uint32_t SampleCountFlagBitsToInt(vk::SampleCountFlagBits bits);

// Return flag-bits corresponding to the specified sample count.  Explode if
// an invalid value is provided.
vk::SampleCountFlagBits SampleCountFlagBitsFromInt(uint32_t sample_count);

// Clip |clippee| so that it is completely contained within |clipper|.
void ClipToRect(vk::Rect2D* clippee, const vk::Rect2D& clipper);

// Check if an Ycbcr format can be used to create VkSamplerYcbcrConversion
// using the Vulkan physical device.
bool IsYuvConversionSupported(vk::PhysicalDevice device, vk::Format format);

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_VULKAN_UTILS_H_
