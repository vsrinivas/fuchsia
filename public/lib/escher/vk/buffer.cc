// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/buffer.h"

#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/resources/resource_manager.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/gpu_allocator.h"

namespace escher {

const ResourceTypeInfo Buffer::kTypeInfo("Buffer", ResourceType::kResource,
                                         ResourceType::kWaitableResource,
                                         ResourceType::kBuffer);

BufferPtr Buffer::New(ResourceManager* manager, GpuMemPtr mem,
                      vk::BufferUsageFlags usage_flags) {
  TRACE_DURATION("gfx", "escher::Buffer::New");
  auto device = manager->vulkan_context().device;

  // Create buffer.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = mem->size();
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer =
      ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

  return fxl::MakeRefCounted<Buffer>(manager, std::move(mem), vk_buffer);
}

Buffer::Buffer(ResourceManager* manager, GpuMemPtr mem, vk::Buffer buffer)
    : WaitableResource(manager), mem_(std::move(mem)), buffer_(buffer) {
  vulkan_context().device.bindBufferMemory(buffer_, mem_->base(),
                                           mem_->offset());
}

Buffer::~Buffer() { vulkan_context().device.destroyBuffer(buffer_); }

}  // namespace escher
