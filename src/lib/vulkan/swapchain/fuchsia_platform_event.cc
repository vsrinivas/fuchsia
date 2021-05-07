// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>

#include "platform_event.h"

#define LOG_VERBOSE(msg, ...) \
  if (true)                   \
  fprintf(stderr, "%s:%d " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__)

std::unique_ptr<PlatformEvent> FuchsiaEvent::Duplicate(VkDevice device,
                                                       VkLayerDispatchTable* dispatch_table) {
  zx::event event;
  zx_status_t status = event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &event);
  if (status != ZX_OK) {
    LOG_VERBOSE("event duplicate failed: %d", status);
    return nullptr;
  }

  return std::make_unique<FuchsiaEvent>(std::move(event));
}

VkResult FuchsiaEvent::ImportToSemaphore(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                         VkSemaphore& semaphore_out) {
  VkImportSemaphoreZirconHandleInfoFUCHSIA import_info = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_ZIRCON_HANDLE_INFO_FUCHSIA,
      .pNext = nullptr,
      .semaphore = semaphore_out,
      .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT_KHR,
      .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_ZIRCON_EVENT_BIT_FUCHSIA,
      .zirconHandle = event_.release()};

  return dispatch_table->ImportSemaphoreZirconHandleFUCHSIA(device, &import_info);
}

PlatformEvent::WaitResult FuchsiaEvent::Wait(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                             uint64_t timeout_ns) {
  zx_signals_t pending;
  zx_status_t status = event_.wait_one(
      ZX_EVENT_SIGNALED,
      timeout_ns == UINT64_MAX ? zx::time::infinite() : zx::deadline_after(zx::nsec(timeout_ns)),
      &pending);

  switch (status) {
    case ZX_OK:
      assert(pending & ZX_EVENT_SIGNALED);
      return WaitResult::Ok;
    case ZX_ERR_TIMED_OUT:
      return WaitResult::TimedOut;
    default:
      LOG_VERBOSE("event wait one failed: %d", status);
      return WaitResult::Error;
  }
}

std::unique_ptr<PlatformEvent> PlatformEvent::Create(VkDevice device,
                                                     VkLayerDispatchTable* dispatch_table,
                                                     bool signaled) {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    LOG_VERBOSE("event create failed: %d", status);
    return nullptr;
  }

  if (signaled) {
    status = event.signal(0, ZX_EVENT_SIGNALED);
    if (status != ZX_OK) {
      LOG_VERBOSE("event signal failed: %d", status);
      return nullptr;
    }
  }

  return std::make_unique<FuchsiaEvent>(std::move(event));
}
