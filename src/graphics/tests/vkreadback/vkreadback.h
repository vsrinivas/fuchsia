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

#include "src/graphics/tests/common/utils.h"
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

  bool Initialize(uint32_t vk_api_verison);
  bool Exec(vk::Fence fence = {});
  bool Submit(vk::Fence fence = {}, bool transition_image = true);
  bool Submit(vk::Semaphore semaphore, uint64_t signal, bool transition_image);
  bool Wait();
  bool Readback();
  vk::Device vulkan_device() const { return ctx_->device().get(); }
  const vk::DispatchLoaderDynamic& vulkan_loader() const { return ctx_->loader(); }
  vk::PhysicalDevice physical_device() const { return ctx_->physical_device(); }
  VulkanExtensionSupportState timeline_semaphore_support() const {
    return timeline_semaphore_support_;
  }

  uint32_t get_exported_memory_handle() const { return exported_memory_handle_; }

 private:
  bool InitVulkan(uint32_t vk_api_version);
  bool InitImage();
  bool InitCommandBuffers();

  bool FillCommandBuffer(vk::CommandBuffer& command_buffer, bool transition_image = true);

  // Finds the first device memory type that can be read by the host.
  //
  // Returns VK_MAX_MEMORY_TYPES if no suitable memory type exists.
  //
  // `allocation_size` is the amount of memory that will be allocated. Only
  // memory types whose backing heaps support allocations of the given size will
  // be considered.
  //
  // `memory_type_bits` is a bit set of acceptable memory types. Bit i is set
  // iff memory type i is an acceptable return value. This is intended to
  // receive the value of a `memoryTypeBits` member in a structure such as
  // VkMemoryRequirements.
  uint32_t FindReadableMemoryType(vk::DeviceSize allocation_size, uint32_t memory_type_bits);

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
  bool use_dedicated_memory_ = false;
  std::unique_ptr<VulkanContext> ctx_;
  vk::UniqueImage image_;
  vk::DeviceMemory device_memory_;

  // Import/export
  vk::DeviceMemory imported_device_memory_;
  uint32_t exported_memory_handle_ = 0;
  ImportExport import_export_;

  vk::UniqueCommandPool command_pool_;
  std::vector<vk::UniqueCommandBuffer> command_buffers_;

  VulkanExtensionSupportState timeline_semaphore_support_ =
      VulkanExtensionSupportState::kNotSupported;

  uint64_t bind_offset_ = 0;

#ifdef __Fuchsia__
  PFN_vkGetMemoryZirconHandleFUCHSIA vkGetMemoryZirconHandleFUCHSIA_{};
  PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA vkGetMemoryZirconHandlePropertiesFUCHSIA_{};
#endif
};

#endif  // SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
