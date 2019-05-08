// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/naive_buffer.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

BufferPtr NaiveBuffer::New(ResourceManager* manager, GpuMemPtr mem,
                           vk::BufferUsageFlags usage_flags) {
  TRACE_DURATION("gfx", "escher::NaiveBuffer::New");
  auto device = manager->vulkan_context().device;

  // Create buffer.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = mem->size();
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer =
      ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

  return fxl::AdoptRef(new NaiveBuffer(manager, std::move(mem), vk_buffer));
}

NaiveBuffer::NaiveBuffer(ResourceManager* manager, GpuMemPtr mem,
                         vk::Buffer buffer)
    : Buffer(manager, buffer, mem->size(), mem->mapped_ptr()),
      mem_(std::move(mem)) {
  FXL_CHECK(vk());
  FXL_CHECK(mem_);
  auto status = vulkan_context().device.bindBufferMemory(vk(), mem_->base(),
                                                         mem_->offset());
  FXL_CHECK(status == vk::Result::eSuccess)
      << "bindBufferMemory failed with status " << (VkResult)status;
}

NaiveBuffer::~NaiveBuffer() { vulkan_context().device.destroyBuffer(vk()); }

}  // namespace impl
}  // namespace escher
