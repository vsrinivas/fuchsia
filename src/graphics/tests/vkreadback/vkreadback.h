// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
#define SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <vector>

#include <vulkan/vulkan.h>

#include "gtest/gtest.h"

// Supports Fuchsia external memory extension.
class VkReadbackTest {
 public:
  static constexpr uint32_t kWidth = 64;
  static constexpr uint32_t kHeight = 64;

  enum Extension { NONE, VK_FUCHSIA_EXTERNAL_MEMORY };

  VkReadbackTest(Extension ext = NONE) : ext_(ext) {}

  bool Initialize();
  bool Exec();
  bool Readback();

  uint32_t get_device_memory_handle() { return device_memory_handle_; }
  void set_device_memory_handle(uint32_t handle) { device_memory_handle_ = handle; }

 private:
  bool InitVulkan();
  bool InitImage();

  Extension ext_;
  bool is_initialized_ = false;
  VkPhysicalDevice vk_physical_device_;
  VkDevice vk_device_;
  VkQueue vk_queue_;
  VkImage vk_image_;
  VkDeviceMemory vk_device_memory_;

  // Import/export
  VkDeviceMemory vk_imported_device_memory_ = VK_NULL_HANDLE;
  uint32_t device_memory_handle_ = 0;
  PFN_vkGetMemoryZirconHandleFUCHSIA vkGetMemoryZirconHandleFUCHSIA_{};
  PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA vkGetMemoryZirconHandlePropertiesFUCHSIA_{};

  VkCommandPool vk_command_pool_;
  VkCommandBuffer vk_command_buffer_;
  uint64_t bind_offset_;
};

#endif  // SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
