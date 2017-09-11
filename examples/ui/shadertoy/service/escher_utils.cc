// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/escher_utils.h"

#include "escher/vk/gpu_mem.h"

namespace shadertoy {

std::pair<escher::SemaphorePtr, mx::event> NewSemaphoreEventPair(
    escher::Escher* escher) {
  mx::event event;
  mx_status_t status = mx::event::create(0u, &event);
  if (status != MX_OK) {
    FXL_LOG(ERROR) << "Failed to create event to import as VkSemaphore.";
    return std::make_pair(escher::SemaphorePtr(), mx::event());
  }

  mx::event event_copy;
  if (event.duplicate(MX_RIGHT_SAME_RIGHTS, &event_copy) != MX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate event.";
    return std::make_pair(escher::SemaphorePtr(), mx::event());
  }

  auto device = escher->device();
  auto sema = escher::Semaphore::New(device->vk_device());

  vk::ImportSemaphoreFdInfoKHR info;
  info.semaphore = sema->vk_semaphore();
  info.fd = event_copy.release();
  info.handleType = vk::ExternalSemaphoreHandleTypeFlagBitsKHR::eSyncFd;

  if (VK_SUCCESS != device->proc_addrs().ImportSemaphoreFdKHR(
                        device->vk_device(),
                        reinterpret_cast<VkImportSemaphoreFdInfoKHR*>(&info))) {
    FXL_LOG(ERROR) << "Failed to import event as VkSemaphore.";
    // Don't leak handle.
    mx_handle_close(info.fd);
    return std::make_pair(escher::SemaphorePtr(), mx::event());
  }

  return std::make_pair(std::move(sema), std::move(event));
}

mx::vmo ExportMemoryAsVMO(escher::Escher* escher,
                          const escher::GpuMemPtr& mem) {
  auto result = escher->vulkan_context().device.exportMemoryMAGMA(mem->base());
  if (result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Failed to export escher::GpuMem as mx::vmo";
    return mx::vmo();
  }
  return mx::vmo(result.value);
}

}  // namespace shadertoy
