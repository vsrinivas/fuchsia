// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/tests/vk_util.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/vmo.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {
namespace test {

vk::DeviceMemory AllocateExportableMemory(vk::Device device, vk::PhysicalDevice physical_device,
                                          vk::MemoryRequirements requirements,
                                          vk::MemoryPropertyFlags flags) {
  uint32_t memory_type_index =
      escher::impl::GetMemoryTypeIndex(physical_device, requirements.memoryTypeBits, flags);
  vk::PhysicalDeviceMemoryProperties memory_types = physical_device.getMemoryProperties();
  if (memory_type_index == memory_types.memoryTypeCount) {
    return nullptr;
  }

  vk::ExportMemoryAllocateInfoKHR export_info;
  export_info.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;

  vk::MemoryAllocateInfo info;
  info.pNext = &export_info;
  info.allocationSize = requirements.size;
  info.memoryTypeIndex = memory_type_index;

  vk::DeviceMemory memory = escher::ESCHER_CHECKED_VK_RESULT(device.allocateMemory(info));
  return memory;
}

MemoryAllocationResult AllocateExportableMemoryDedicatedToImageIfRequired(
    vk::Device device, vk::PhysicalDevice physical_device, vk::DeviceSize requested_size,
    vk::Image dedicated_image, vk::MemoryPropertyFlags flags,
    const vk::DispatchLoaderDynamic& dispatch) {
  // We use the StructureChain provided by vulkan.hpp (see https://github.com/
  // KhronosGroup/Vulkan-Hpp/blob/HEAD/README.md for details ) to get chaining
  // of MemoryRequirements2 and MemoryDedicatedRequirementsKHR structures.
  auto get_requirements_result =
      device.getImageMemoryRequirements2KHR<vk::MemoryRequirements2KHR,
                                            vk::MemoryDedicatedRequirementsKHR>(dedicated_image,
                                                                                dispatch);
  auto memory_requirements =
      get_requirements_result.get<vk::MemoryRequirements2KHR>().memoryRequirements;
  auto dedicated_requirements = get_requirements_result.get<vk::MemoryDedicatedRequirementsKHR>();

  if (dedicated_requirements.requiresDedicatedAllocation) {
    // Allocate dedicated memory, we use the allocation requirement of the image
    // as the memory allocation info.
    uint32_t size = memory_requirements.size;
    uint32_t type = escher::impl::GetMemoryTypeIndex(physical_device,
                                                     memory_requirements.memoryTypeBits, flags);
    vk::PhysicalDeviceMemoryProperties memory_types = physical_device.getMemoryProperties();
    if (type == memory_types.memoryTypeCount) {
      // We cannot find any memory types available for this image.
      return MemoryAllocationResult{nullptr, 0u, false};
    }

    vk::StructureChain<vk::MemoryAllocateInfo, vk::ExportMemoryAllocateInfoKHR,
                       vk::MemoryDedicatedAllocateInfoKHR>
        allocate_info_chain = {vk::MemoryAllocateInfo(size, type),
                               vk::ExportMemoryAllocateInfoKHR(
                                   vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA),
                               vk::MemoryDedicatedAllocateInfoKHR(dedicated_image, vk::Buffer())};
    auto result = device.allocateMemory(allocate_info_chain.get<vk::MemoryAllocateInfo>());
    FX_DCHECK(result.result == vk::Result::eSuccess);
    return MemoryAllocationResult{result.value, size, true};
  } else {
    // Allocate non-dedicated memory.
    // We use the passed size argument in the memory allocation info. For memory
    // type, we use 0xFFFFFFFF to represent *any* possible memory type supported
    // by device as long as they support the given |flags|.
    vk::MemoryRequirements requirements;
    requirements.size = requested_size;
    requirements.alignment = vk::DeviceSize{};
    requirements.memoryTypeBits = 0xFFFFFFFF;
    vk::DeviceMemory result =
        AllocateExportableMemory(device, physical_device, requirements, flags);
    return MemoryAllocationResult{result, requested_size, false};
  }
}

zx::vmo ExportMemoryAsVmo(vk::Device device, vk::DispatchLoaderDynamic dispatch_loader,
                          vk::DeviceMemory memory) {
  vk::MemoryGetZirconHandleInfoFUCHSIA export_memory_info(
      memory, vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  auto result = device.getMemoryZirconHandleFUCHSIA(export_memory_info, dispatch_loader);
  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Failed to export vk::DeviceMemory as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

vk::MemoryRequirements GetBufferRequirements(vk::Device device, vk::DeviceSize size,
                                             vk::BufferUsageFlags usage_flags) {
  // Create a temp buffer to find out memory requirements.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = size;
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer = escher::ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));
  auto retval = device.getBufferMemoryRequirements(vk_buffer);
  device.destroyBuffer(vk_buffer);
  return retval;
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
