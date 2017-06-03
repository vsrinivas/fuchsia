// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/gpu_memory.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo GpuMemory::kTypeInfo = {
    ResourceType::kMemory | ResourceType::kGpuMemory, "GpuMemory"};

GpuMemory::GpuMemory(Session* session,
                     vk::Device device,
                     vk::DeviceMemory mem,
                     vk::DeviceSize size,
                     uint32_t memory_type_index)
    : Memory(session, GpuMemory::kTypeInfo),
      mem_(escher::GpuMem::New(device, mem, size, memory_type_index)) {}

GpuMemoryPtr GpuMemory::New(Session* session,
                            vk::Device device,
                            const mozart2::MemoryPtr& args,
                            ErrorReporter* error_reporter) {
  // TODO: Need to change driver semantics so that you can import a VMO twice.

  if (!device) {
    error_reporter->ERROR() << "composer::Session::CreateMemory(): "
                               "Getting VkDevice failed.";
    return nullptr;
  }
  vk::DeviceMemory memory = nullptr;

  size_t vmo_size;
  args->vmo.get_size(&vmo_size);

  // Import a VkDeviceMemory from the VMO. vkImportDeviceMemoryMAGMA takes
  // ownership of the VMO handle it is passed.
  vk::Result err =
      device.importMemoryMAGMA(args->vmo.release(), nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    error_reporter->ERROR() << "composer::Session::CreateMemory(): "
                               "vkImportDeviceMemoryMAGMA failed.";
    return nullptr;
  }

  // TODO: Need to be able to get the memory type index in
  // vkImportDeviceMemoryMAGMA.
  uint32_t memory_type_index = 0;

  return ftl::MakeRefCounted<GpuMemory>(
      session, vk::Device(device), vk::DeviceMemory(memory),
      vk::DeviceSize(vmo_size), memory_type_index);
}

}  // namespace composer
}  // namespace mozart
