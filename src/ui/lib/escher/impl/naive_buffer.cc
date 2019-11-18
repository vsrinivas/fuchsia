// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/naive_buffer.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {
namespace impl {

namespace {

bool CheckBufferMemoryRequirements(ResourceManager* manager, vk::Buffer vk_buffer,
                                   const GpuMemPtr& mem) {
  auto mem_requirements = manager->vk_device()
                              .getBufferMemoryRequirements2KHR<vk::MemoryRequirements2KHR,
                                                               vk::MemoryDedicatedRequirementsKHR>(
                                  vk_buffer, manager->vulkan_context().loader);

  auto size_required = mem_requirements.get<vk::MemoryRequirements2KHR>().memoryRequirements.size;
  auto alignment_required =
      mem_requirements.get<vk::MemoryRequirements2KHR>().memoryRequirements.alignment;

  if (mem->size() < size_required) {
    FXL_LOG(ERROR) << "Memory requirements check failed: Buffer requires " << size_required
                   << " bytes of memory, while the provided mem size is " << mem->size()
                   << " bytes.";
    return false;
  }

  if (mem->offset() % alignment_required != 0) {
    FXL_LOG(ERROR) << "Memory requirements check failed: Buffer requires alignment of "
                   << alignment_required << " bytes, while the provided mem offset is "
                   << mem->offset();
    return false;
  }

  return true;
}

}  // namespace

BufferPtr NaiveBuffer::New(ResourceManager* manager, GpuMemPtr mem,
                           vk::BufferUsageFlags usage_flags) {
  TRACE_DURATION("gfx", "escher::NaiveBuffer::New");
  auto device = manager->vulkan_context().device;
  auto size = mem->size();

  // Create buffer.
  vk::BufferCreateInfo buffer_create_info;
  buffer_create_info.size = mem->size();
  buffer_create_info.usage = usage_flags;
  buffer_create_info.sharingMode = vk::SharingMode::eExclusive;
  auto vk_buffer = ESCHER_CHECKED_VK_RESULT(device.createBuffer(buffer_create_info));

  // Check buffer memory requirements before binding the buffer to memory.
  if (!CheckBufferMemoryRequirements(manager, vk_buffer, mem)) {
    FXL_LOG(ERROR) << "NaiveBuffer::New() Failed: cannot satisfy memory requirements.";
    return nullptr;
  }

  return fxl::AdoptRef(new NaiveBuffer(manager, std::move(mem), size, vk_buffer));
}

BufferPtr NaiveBuffer::AdoptVkBuffer(ResourceManager* manager, GpuMemPtr mem,
                                     vk::DeviceSize vk_buffer_size, vk::Buffer vk_buffer) {
  TRACE_DURATION("gfx", "escher::NaiveBuffer::AdoptVkBuffer");

  // Check buffer memory requirements before binding the buffer to memory.
  if (!CheckBufferMemoryRequirements(manager, vk_buffer, mem)) {
    FXL_LOG(ERROR) << "NaiveBuffer::AdoptVkBuffer() Failed: cannot satisfy memory requirements.";
    return nullptr;
  }

  return fxl::AdoptRef(new NaiveBuffer(manager, std::move(mem), vk_buffer_size, vk_buffer));
}

NaiveBuffer::NaiveBuffer(ResourceManager* manager, GpuMemPtr mem, vk::DeviceSize vk_buffer_size,
                         vk::Buffer buffer)
    : Buffer(manager, buffer, vk_buffer_size, mem->mapped_ptr()), mem_(std::move(mem)) {
  FXL_CHECK(vk());
  FXL_CHECK(mem_);

  auto status = vulkan_context().device.bindBufferMemory(vk(), mem_->base(), mem_->offset());
  FXL_CHECK(status == vk::Result::eSuccess)
      << "bindBufferMemory failed with status " << (VkResult)status;
}

NaiveBuffer::~NaiveBuffer() { vulkan_context().device.destroyBuffer(vk()); }

}  // namespace impl
}  // namespace escher
