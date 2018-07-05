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

  vk::ImportSemaphoreFuchsiaHandleInfoKHR info;
  info.semaphore = sema->vk_semaphore();
  info.handle = event_copy.release();
  info.handleType = vk::ExternalSemaphoreHandleTypeFlagBitsKHR::eFuchsiaFence;

  if (VK_SUCCESS !=
      device->proc_addrs().ImportSemaphoreFuchsiaHandleKHR(
          device->vk_device(),
          reinterpret_cast<VkImportSemaphoreFuchsiaHandleInfoKHR*>(&info))) {
    FXL_LOG(ERROR) << "Failed to import event as VkSemaphore.";
    // Don't leak handle.
    zx_handle_close(info.handle);
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  return std::make_pair(std::move(sema), std::move(event));
}

zx::event GetEventForSemaphore(
    const escher::VulkanDeviceQueues::ProcAddrs& proc_addresses,
    const vk::Device& device, const escher::SemaphorePtr& semaphore) {
  zx_handle_t semaphore_handle;

  vk::SemaphoreGetFuchsiaHandleInfoKHR info(
      semaphore->vk_semaphore(),
      vk::ExternalSemaphoreHandleTypeFlagBitsKHR::eFuchsiaFence);

  VkResult result = proc_addresses.GetSemaphoreFuchsiaHandleKHR(
      device,
      reinterpret_cast<const VkSemaphoreGetFuchsiaHandleInfoKHR*>(&info),
      &semaphore_handle);

  if (result != VK_SUCCESS) {
    FXL_LOG(WARNING) << "unable to export semaphore";
    return zx::event();
  }
  return zx::event(semaphore_handle);
}

zx::vmo ExportMemoryAsVmo(escher::Escher* escher,
                          const escher::GpuMemPtr& mem) {
  vk::MemoryGetFuchsiaHandleInfoKHR export_memory_info(
      mem->base(), vk::ExternalMemoryHandleTypeFlagBitsKHR::eFuchsiaVmo);
  auto result = escher->device()->proc_addrs().getMemoryFuchsiaHandleKHR(
      escher->vulkan_context().device, export_memory_info);
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Failed to export escher::GpuMem as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

}  // namespace escher
