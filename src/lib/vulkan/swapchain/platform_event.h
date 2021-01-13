// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_VULKAN_SWAPCHAIN_PLATFORM_EVENT_H_
#define SRC_LIB_VULKAN_SWAPCHAIN_PLATFORM_EVENT_H_

#include <vulkan/vulkan.h>

#include "vk_dispatch_table_helper.h"
#include "vulkan/vk_layer.h"

class PlatformEvent {
 public:
  virtual ~PlatformEvent() = default;

  static std::unique_ptr<PlatformEvent> Create(VkDevice device,
                                               VkLayerDispatchTable* dispatch_table, bool signaled);

  // Transfers ownership into the given semaphore
  virtual VkResult ImportToSemaphore(VkDevice device, VkLayerDispatchTable* dispatch_table,
                                     VkSemaphore& semaphore_out) = 0;

  virtual std::unique_ptr<PlatformEvent> Duplicate(VkDevice device,
                                                   VkLayerDispatchTable* dispatch_table) = 0;

  enum class WaitResult { Ok, TimedOut, Error };

  virtual WaitResult Wait(VkDevice device, VkLayerDispatchTable* dispatch_table,
                          uint64_t timeout_ns) = 0;

  // Move-only
  PlatformEvent(PlatformEvent&) = delete;
  PlatformEvent& operator=(PlatformEvent&) = delete;

 protected:
  PlatformEvent() = default;
};

#if defined(__Fuchsia__)

#include <lib/zx/event.h>

// We use a zircon event on fuchsia because we don't support external fences.
class FuchsiaEvent : public PlatformEvent {
 public:
  explicit FuchsiaEvent(zx::event event) : event_(std::move(event)) {}

  std::unique_ptr<PlatformEvent> Duplicate(VkDevice device,
                                           VkLayerDispatchTable* dispatch_table) override;

  VkResult ImportToSemaphore(VkDevice device, VkLayerDispatchTable* dispatch_table,
                             VkSemaphore& semaphore_out) override;

  WaitResult Wait(VkDevice device, VkLayerDispatchTable* dispatch_table,
                  uint64_t timeout_ns) override;

  zx::event Take() { return std::move(event_); }

 private:
  zx::event event_;
};

#elif defined(__linux__)

// We use a Vulkan fence on Linux because there's no convenient OS primitive.
class LinuxEvent : public PlatformEvent {
 public:
  explicit LinuxEvent(VkFence fence) : fence_(fence) {}

  std::unique_ptr<PlatformEvent> Duplicate(VkDevice device,
                                           VkLayerDispatchTable* dispatch_table) override;

  WaitResult Wait(VkDevice device, VkLayerDispatchTable* dispatch_table,
                  uint64_t timeout_ns) override;

  VkResult ImportToSemaphore(VkDevice device, VkLayerDispatchTable* dispatch_table,
                             VkSemaphore& semaphore_out) override;

 private:
  VkFence fence_;
};

#endif

#endif  // SRC_LIB_VULKAN_SWAPCHAIN_PLATFORM_EVENT_H_
