// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
#define SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <gtest/gtest.h>
#include <vulkan/vulkan.h>

#include "src/graphics/tests/common/vulkan_context.h"

// Supports Fuchsia external memory extension.
class VkReadbackTest {
 public:
  static constexpr uint32_t kWidth = 64;
  static constexpr uint32_t kHeight = 64;

  // One command buffer to transition the host visible |image_|.
  // One command buffer, post transition of |image_|.
  static constexpr size_t kNumCommandBuffers = 2;

  enum Extension { NONE, VK_FUCHSIA_EXTERNAL_MEMORY };

  // Depending on how the test is initialized, it may be a self contained
  // instance, an instance that imports external memory or an instance that
  // exports external memory.
  enum ImportExport { SELF, IMPORT_EXTERNAL_MEMORY, EXPORT_EXTERNAL_MEMORY };

  // Constructor for a self contained instance or an instance that exports
  // its external memory handle.
  explicit VkReadbackTest(Extension ext = NONE);

  // Constructor for an instance that imports an external memory handle.
  explicit VkReadbackTest(uint32_t exported_memory_handle);

  virtual ~VkReadbackTest();

  bool Initialize();
  bool Exec(vk::Fence fence = {});
  bool Submit(vk::Fence fence = {}, bool transition_image = true);
  bool Wait();
  bool Readback();
  vk::Device vulkan_device() const { return ctx_->device().get(); }

  uint32_t get_exported_memory_handle() const { return exported_memory_handle_; }

 private:
  bool InitVulkan();
  bool InitImage();
  bool InitCommandBuffers();

  bool FillCommandBuffer(vk::CommandBuffer &command_buffer, bool transition_image = true);

#ifdef __Fuchsia__
  bool AllocateFuchsiaImportedMemory(uint32_t device_memory_handle);
  bool AssignExportedMemoryHandle();
  void VerifyExpectedImageFormats() const;
#endif

  Extension ext_;
  bool is_initialized_ = false;
  bool vulkan_initialized_ = false;
  bool image_initialized_ = false;
  bool command_buffers_initialized_ = false;
  std::unique_ptr<VulkanContext> ctx_;
  vk::UniqueImage image_;
  vk::DeviceMemory device_memory_;

  // Import/export
  vk::DeviceMemory imported_device_memory_;
  uint32_t exported_memory_handle_ = 0;
  ImportExport import_export_;

  vk::UniqueCommandPool command_pool_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;

  uint64_t bind_offset_ = 0;

#ifdef __Fuchsia__
  PFN_vkGetMemoryZirconHandleFUCHSIA vkGetMemoryZirconHandleFUCHSIA_{};
  PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA vkGetMemoryZirconHandlePropertiesFUCHSIA_{};
#endif
};

#endif  // SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
