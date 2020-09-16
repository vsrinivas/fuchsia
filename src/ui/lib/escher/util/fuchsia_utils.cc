// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/fuchsia_utils.h"

#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {

std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(escher::Escher* escher) {
  zx::event event;
  zx_status_t status = zx::event::create(0u, &event);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create event to import as VkSemaphore.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate event.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  auto device = escher->device();
  auto sema = escher::Semaphore::New(device->vk_device());

  vk::ImportSemaphoreZirconHandleInfoFUCHSIA info;
  info.semaphore = sema->vk_semaphore();
  info.handle = event_copy.release();
  info.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eTempZirconEventFUCHSIA;

  if (vk::Result::eSuccess != device->vk_device().importSemaphoreZirconHandleFUCHSIA(
                                  info, escher->device()->dispatch_loader())) {
    FX_LOGS(ERROR) << "Failed to import event as VkSemaphore.";
    // Don't leak handle.
    zx_handle_close(info.handle);
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  return std::make_pair(std::move(sema), std::move(event));
}

zx::event GetEventForSemaphore(VulkanDeviceQueues* device, const escher::SemaphorePtr& semaphore) {
  vk::SemaphoreGetZirconHandleInfoFUCHSIA info(
      semaphore->vk_semaphore(), vk::ExternalSemaphoreHandleTypeFlagBits::eTempZirconEventFUCHSIA);

  auto result =
      device->vk_device().getSemaphoreZirconHandleFUCHSIA(info, device->dispatch_loader());

  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(WARNING) << "unable to export semaphore";
    return zx::event();
  }
  return zx::event(result.value);
}

zx::vmo ExportMemoryAsVmo(escher::Escher* escher, const escher::GpuMemPtr& mem) {
  vk::MemoryGetZirconHandleInfoFUCHSIA export_memory_info(
      mem->base(), vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  auto result = escher->vk_device().getMemoryZirconHandleFUCHSIA(
      export_memory_info, escher->device()->dispatch_loader());
  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Failed to export escher::GpuMem as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

std::pair<escher::GpuMemPtr, escher::ImagePtr> GenerateExportableMemImage(
    vk::Device device, escher::ResourceManager* resource_manager,
    const escher::ImageInfo& image_info) {
  FX_DCHECK(device);
  FX_DCHECK(resource_manager);
  FX_DCHECK(image_info.is_external);

  // Create vk::Image
  constexpr auto kInitialLayout = vk::ImageLayout::ePreinitialized;
  auto create_info = escher::image_utils::CreateVkImageCreateInfo(image_info, kInitialLayout);
  vk::Image vk_image = device.createImage(create_info).value;

  vk::MemoryRequirements reqs = device.getImageMemoryRequirements(vk_image);
  uint32_t memory_type = 0;
  for (; memory_type < 32; memory_type++) {
    if ((reqs.memoryTypeBits & (1U << memory_type))) {
      break;
    }
  }

  // Allocate vk::Memory.
  vk::MemoryDedicatedAllocateInfo dedicated_allocate_info(vk_image, vk::Buffer());
  vk::ExportMemoryAllocateInfoKHR export_allocate_info(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  export_allocate_info.setPNext(&dedicated_allocate_info);

  vk::MemoryAllocateInfo alloc_info(reqs.size, memory_type);
  alloc_info.setPNext(&export_allocate_info);
  auto vk_memory = device.allocateMemory(alloc_info).value;

  // Convert vk::DeviceMemory to escher::GpuMem, vk::Image to escher::Image
  auto mem =
      escher::GpuMem::AdoptVkMemory(device, vk_memory, reqs.size, false /* needs_mapped_ptr */);
  auto image = escher::impl::NaiveImage::AdoptVkImage(resource_manager, image_info, vk_image, mem,
                                                      kInitialLayout);
  return std::make_pair(mem, image);
}

vk::Format SysmemPixelFormatTypeToVkFormat(fuchsia::sysmem::PixelFormatType pixel_format) {
  switch (pixel_format) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return vk::Format::eB8G8R8A8Srgb;
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return vk::Format::eR8G8B8A8Srgb;
    case fuchsia::sysmem::PixelFormatType::NV12:
      return vk::Format::eG8B8R82Plane420Unorm;
    case fuchsia::sysmem::PixelFormatType::I420:
      return vk::Format::eG8B8R83Plane420Unorm;
    default:
      break;
  }
  return vk::Format::eUndefined;
}

}  // namespace escher
