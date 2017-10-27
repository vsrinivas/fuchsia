// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"

#include "lib/images/fidl/memory_type.fidl.h"

namespace scene_manager {

const ResourceTypeInfo GpuMemory::kTypeInfo = {
    ResourceType::kMemory | ResourceType::kGpuMemory, "GpuMemory"};

GpuMemory::GpuMemory(Session* session,
                     scenic::ResourceId id,
                     vk::Device device,
                     vk::DeviceMemory mem,
                     vk::DeviceSize size,
                     uint32_t memory_type_index)
    : Memory(session, id, GpuMemory::kTypeInfo),
      escher_gpu_mem_(
          escher::GpuMem::New(device, mem, size, memory_type_index)) {}

GpuMemoryPtr GpuMemory::New(Session* session,
                            scenic::ResourceId id,
                            vk::Device device,
                            const scenic::MemoryPtr& args,
                            ErrorReporter* error_reporter) {
  if (args->memory_type != scenic::MemoryType::VK_DEVICE_MEMORY) {
    error_reporter->ERROR() << "scene_manager::GpuMemory::New(): "
                               "Memory must be of type VK_DEVICE_MEMORY.";
    return nullptr;
  }
  return New(session, id, device, std::move(args->vmo), error_reporter);
}

GpuMemoryPtr GpuMemory::New(Session* session,
                            scenic::ResourceId id,
                            vk::Device device,
                            zx::vmo vmo,
                            ErrorReporter* error_reporter) {
  // TODO: Need to change driver semantics so that you can import a VMO twice.

  if (!device) {
    error_reporter->ERROR() << "scene_manager::Session::CreateMemory(): "
                               "Getting VkDevice failed.";
    return nullptr;
  }
  vk::DeviceMemory memory = nullptr;

  size_t vmo_size;
  vmo.get_size(&vmo_size);

  // Import a VkDeviceMemory from the VMO. VkAllocateMemory takes ownership of
  // the VMO handle it is passed.
  vk::ImportMemoryFuchsiaHandleInfoKHR memory_import_info(
      vk::ExternalMemoryHandleTypeFlagBitsKHR::eFuchsiaVmo, vmo.release());

  vk::MemoryAllocateInfo memory_allocate_info(vmo_size);
  memory_allocate_info.setPNext(&memory_import_info);

  vk::Result err =
      device.allocateMemory(&memory_allocate_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    error_reporter->ERROR() << "scene_manager::Session::CreateMemory(): "
                               "VkAllocateMemory failed.";
    return nullptr;
  }

  // TODO(MZ-388): Need to be able to get the memory type index using
  // vkGetMemoryFuchsiaHandlePropertiesKHR.
  uint32_t memory_type_index = 0;

  return fxl::MakeRefCounted<GpuMemory>(
      session, id, vk::Device(device), vk::DeviceMemory(memory),
      vk::DeviceSize(vmo_size), memory_type_index);
}

}  // namespace scene_manager
