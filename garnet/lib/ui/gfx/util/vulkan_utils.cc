// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/util/vulkan_utils.h"
#include "lib/escher/impl/vulkan_utils.h"

#include "lib/escher/vk/gpu_mem.h"

namespace scenic_impl {
namespace gfx {

uint32_t GetImportedMemoryTypeIndex(vk::PhysicalDevice physical_device,
                                    vk::Device device) {
  // Is there a better way to get the memory type bits than creating and
  // immediately destroying a buffer?
  // TODO(SCN-79): Use sysmem for this when it's available.
  constexpr vk::DeviceSize kUnimportantBufferSize = 30000;
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = kUnimportantBufferSize;
  buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc |
                             vk::BufferUsageFlagBits::eTransferDst |
                             vk::BufferUsageFlagBits::eStorageTexelBuffer |
                             vk::BufferUsageFlagBits::eStorageBuffer |
                             vk::BufferUsageFlagBits::eIndexBuffer |
                             vk::BufferUsageFlagBits::eVertexBuffer;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer =
      escher::ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

  vk::MemoryRequirements reqs = device.getBufferMemoryRequirements(vk_buffer);
  device.destroyBuffer(vk_buffer);

  // TODO(SCN-998): Decide how to determine if we're on an UMA platform
  // or not.
  return escher::impl::GetMemoryTypeIndex(
      physical_device, reqs.memoryTypeBits,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
}

}  // namespace gfx
}  // namespace scenic_impl
