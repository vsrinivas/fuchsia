// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "platform_event.h"

#define LOG_VERBOSE(msg, ...) \
  if (true)                   \
  fprintf(stderr, "%s:%d " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__)

std::unique_ptr<PlatformEvent> LinuxEvent::Duplicate(VkDevice device,
                                                     VkLayerDispatchTable* dispatch_table) {
  // Export from our fence into newly created fence.
  VkFenceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
  };

  VkFence fence;
  VkResult result = dispatch_table->CreateFence(device, &create_info,
                                                nullptr,  // pAllocator
                                                &fence);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("CreateFence failed: %d", result);
    return nullptr;
  }

  VkFenceGetFdInfoKHR get_fd_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR,
      .pNext = nullptr,
      .fence = fence_,
      .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT,
  };

  int fd;
  result = dispatch_table->GetFenceFdKHR(device, &get_fd_info, &fd);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("GetFenceFdKHR failed: %d", result);
    dispatch_table->DestroyFence(device, fence,
                                 nullptr  // pAllocator
    );
    return nullptr;
  }

  // Import to fence
  VkImportFenceFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR,
      .pNext = nullptr,
      .fence = fence,
      .flags = 0,
      .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT,
      .fd = fd,
  };

  result = dispatch_table->ImportFenceFdKHR(device, &import_info);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("ImportFenceFdKHR failed: %d", result);
    dispatch_table->DestroyFence(device, fence,
                                 nullptr  // pAllocator
    );
    // The fd is left open; TODO(fxbug.dev/67565) close the fd
    return nullptr;
  }

  return std::make_unique<LinuxEvent>(fence);
}

PlatformEvent::WaitResult LinuxEvent::Wait(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                           uint64_t timeout_ns) {
  VkResult result = dispatch_table->WaitForFences(device, 1, &fence_, VK_TRUE, timeout_ns);

  switch (result) {
    case VK_SUCCESS:
      return WaitResult::Ok;
    case VK_TIMEOUT:
      return WaitResult::TimedOut;
    default:
      LOG_VERBOSE("WaitForFences failed: %d", result);
      return WaitResult::Error;
  }
}

VkResult LinuxEvent::ImportToSemaphore(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                       VkSemaphore& semaphore_out) {
  // Export from fence
  VkFenceGetFdInfoKHR get_fd_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR,
      .pNext = nullptr,
      .fence = fence_,
      .handleType = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT,
  };

  int fd = 0;
  VkResult result = dispatch_table->GetFenceFdKHR(device, &get_fd_info, &fd);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("GetFenceFdKHR failed: %d", result);
    return result;
  }

  // Destroy fence for ownership transfer
  dispatch_table->DestroyFence(device, fence_,
                               nullptr  // pAllocator
  );
  fence_ = VK_NULL_HANDLE;

  // Import to semaphore
  VkImportSemaphoreFdInfoKHR import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
      .pNext = nullptr,
      .semaphore = semaphore_out,
      .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
      .fd = fd,
  };

  result = dispatch_table->ImportSemaphoreFdKHR(device, &import_info);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("ImportSemaphoreFdKHR failed: %d", result);
    // The fd is left open; TODO(fxbug.dev/67565) close the fd
    return result;
  }

  return VK_SUCCESS;
}

std::unique_ptr<PlatformEvent> PlatformEvent::Create(VkDevice device,
                                                     VkLayerDispatchTable* dispatch_table,
                                                     bool signaled) {
  VkExportFenceCreateInfo export_create_info = {
    .sType = VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO,
    .pNext = nullptr,
    .handleTypes = VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT_KHR
  };
  VkFenceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = &export_create_info,
      .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u,
  };

  VkFence fence;
  VkResult result = dispatch_table->CreateFence(device, &create_info,
                                                nullptr,  // pAllocator,
                                                &fence);
  if (result != VK_SUCCESS) {
    LOG_VERBOSE("CreateFence failed: %d", result);
    return nullptr;
  }

  return std::make_unique<LinuxEvent>(fence);
}
