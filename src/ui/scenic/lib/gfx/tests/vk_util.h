// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_TESTS_VK_UTIL_H_
#define SRC_UI_SCENIC_LIB_GFX_TESTS_VK_UTIL_H_

#include <lib/zx/vmo.h>

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {
namespace test {

struct MemoryAllocationResult {
  vk::DeviceMemory device_memory;
  vk::DeviceSize size;
  bool is_dedicated;
};

// This function allocates a DeviceMemory which can be exported as a vmo object.
//
// For devices requires dedicated allocation to image, this function will
// allocate a VkDeviceMemory dedicated to |dedicated_image|, while for devices
// that doesn't require dedicated allocation, this function will not use
// |dedicated_image| argument and will return a normal allocation.
//
// Returns a MemoryAllocationResult struct.
MemoryAllocationResult AllocateExportableMemoryDedicatedToImageIfRequired(
    vk::Device device, vk::PhysicalDevice physical_device, vk::DeviceSize requested_size,
    vk::Image dedicated_image, vk::MemoryPropertyFlags flags,
    const vk::DispatchLoaderDynamic& dispatch);

// This function allocates a DeviceMemory which can be exported as a vmo object.
//
// This function only allocates memory *NOT* dedicated to a specific image, so
// it will not work on devices requires dedicated allocation (which can be
// checked by calling vkGetImageMemoryRequirements2() function).
vk::DeviceMemory AllocateExportableMemory(vk::Device device, vk::PhysicalDevice physical_device,
                                          vk::MemoryRequirements requirements,
                                          vk::MemoryPropertyFlags flags);

// Export an exportable vk::DeviceMemory as an zx::vmo object.
//
// vk::DeviceMemory should be allocated as an exportable memory (image
// dedication may be required per vkGetImageMemoryRequirements2() results).
//
// Behavior of exporting non-exportable memory is undefined.
zx::vmo ExportMemoryAsVmo(vk::Device device, vk::DispatchLoaderDynamic dispatch_loader,
                          vk::DeviceMemory memory);

vk::MemoryRequirements GetBufferRequirements(vk::Device device, vk::DeviceSize size,
                                             vk::BufferUsageFlags usage_flags);

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_TESTS_VK_UTIL_H_
