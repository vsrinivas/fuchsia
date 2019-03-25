// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/fuchsia_utils.h"

#include "lib/escher/vk/gpu_mem.h"

namespace escher {

std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(
    escher::Escher* escher) {
  zx::event event;
  zx_status_t status = zx::event::create(0u, &event);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create event to import as VkSemaphore.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate event.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  auto device = escher->device();
  auto sema = escher::Semaphore::New(device->vk_device());

  vk::ImportSemaphoreZirconHandleInfoFUCHSIA info;
  info.semaphore = sema->vk_semaphore();
  info.handle = event_copy.release();
  info.handleType =
      vk::ExternalSemaphoreHandleTypeFlagBits::eTempZirconEventFUCHSIA;

  if (vk::Result::eSuccess !=
      device->vk_device().importSemaphoreZirconHandleFUCHSIA(
          info, escher->device()->dispatch_loader())) {
    FXL_LOG(ERROR) << "Failed to import event as VkSemaphore.";
    // Don't leak handle.
    zx_handle_close(info.handle);
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  return std::make_pair(std::move(sema), std::move(event));
}

zx::event GetEventForSemaphore(VulkanDeviceQueues* device,
                               const escher::SemaphorePtr& semaphore) {
  vk::SemaphoreGetZirconHandleInfoFUCHSIA info(
      semaphore->vk_semaphore(),
      vk::ExternalSemaphoreHandleTypeFlagBits::eTempZirconEventFUCHSIA);

  auto result = device->vk_device().getSemaphoreZirconHandleFUCHSIA(
      info, device->dispatch_loader());

  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(WARNING) << "unable to export semaphore";
    return zx::event();
  }
  return zx::event(result.value);
}

zx::vmo ExportMemoryAsVmo(escher::Escher* escher,
                          const escher::GpuMemPtr& mem) {
  vk::MemoryGetZirconHandleInfoFUCHSIA export_memory_info(
      mem->base(), vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  auto result = escher->vk_device().getMemoryZirconHandleFUCHSIA(
      export_memory_info, escher->device()->dispatch_loader());
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Failed to export escher::GpuMem as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

}  // namespace escher
