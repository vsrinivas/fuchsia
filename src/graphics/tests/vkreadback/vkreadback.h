// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
#define SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_

#include <cstdint>
#include <optional>
#include <unordered_map>

#include <gtest/gtest.h>

#include "src/graphics/tests/common/utils.h"
#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

#ifdef __Fuchsia__
#include <lib/zx/vmo.h>
#endif

struct VkReadbackSubmitOptions {
  // The first submission must include an image transition.
  bool include_start_transition = false;

  // The last submission before Readback() must include an image barrier.
  bool include_end_barrier = false;
};

template <>
struct std::hash<VkReadbackSubmitOptions> {
  size_t operator()(const VkReadbackSubmitOptions& options) const {
    return (options.include_start_transition ? 2 : 0) | (options.include_end_barrier ? 1 : 0);
  }
};

inline bool operator==(const VkReadbackSubmitOptions& lhs, const VkReadbackSubmitOptions& rhs) {
  return lhs.include_start_transition == rhs.include_start_transition &&
         lhs.include_end_barrier == rhs.include_end_barrier;
}

// Supports Fuchsia external memory extension.
class VkReadbackTest {
 public:
  static constexpr int kWidth = 64;
  static constexpr int kHeight = 64;

  enum Extension { NONE, VK_FUCHSIA_EXTERNAL_MEMORY };

  // Depending on how the test is initialized, it may be a self contained
  // instance, an instance that imports external memory or an instance that
  // exports external memory.
  enum ImportExport { SELF, IMPORT_EXTERNAL_MEMORY, EXPORT_EXTERNAL_MEMORY };

  // Constructor for a self contained instance or an instance that exports
  // its external memory handle.
  explicit VkReadbackTest(Extension ext = NONE);

#ifdef __Fuchsia__
  // Constructor for an instance that imports an external memory VMO.
  explicit VkReadbackTest(zx::vmo exported_memory_vmo);
#endif

  virtual ~VkReadbackTest();

  [[nodiscard]] bool Initialize(uint32_t vk_api_verison);

  [[nodiscard]] bool Exec(vk::Fence fence = {});
  [[nodiscard]] bool Submit(VkReadbackSubmitOptions options, vk::Fence fence = {});
  [[nodiscard]] bool Submit(VkReadbackSubmitOptions options, vk::Semaphore semaphore,
                            uint64_t signal);
  [[nodiscard]] bool Wait();

  // Reflects a Submit() executed by the VkReadbackTest that exported the memory
  // handle imported by this test.
  void TransferSubmittedStateFrom(const VkReadbackTest& export_source);

  [[nodiscard]] bool Readback();

  vk::Device vulkan_device() const { return ctx_->device().get(); }
  const vk::DispatchLoaderDynamic& vulkan_loader() const { return ctx_->loader(); }
  vk::PhysicalDevice physical_device() const { return ctx_->physical_device(); }
  VulkanExtensionSupportState timeline_semaphore_support() const {
    return timeline_semaphore_support_;
  }

#ifdef __Fuchsia__
  [[nodiscard]] zx::vmo TakeExportedMemoryVmo();
#endif

 private:
  [[nodiscard]] bool InitVulkan(uint32_t vk_api_version);
  [[nodiscard]] bool InitImage();
  [[nodiscard]] bool InitCommandBuffers();

  [[nodiscard]] bool FillCommandBuffer(VkReadbackSubmitOptions options,
                                       vk::UniqueCommandBuffer command_buffer);

  // Must be called by each Submit() variant exactly once.
  //
  // The validation performed by this method is not idempotent.
  void ValidateSubmitOptions(VkReadbackSubmitOptions options);

  // Finds the first device memory type that can be read by the host.
  //
  // Returns nullopt if no suitable memory type exists.
  //
  // `allocation_size` is the amount of memory that will be allocated. Only
  // memory types whose backing heaps support allocations of the given size will
  // be considered.
  //
  // `memory_type_bits` is a bit set of acceptable memory types. Bit i is set
  // iff memory type i is an acceptable return value. This is intended to
  // receive the value of a `memoryTypeBits` member in a structure such as
  // VkMemoryRequirements.
  std::optional<uint32_t> FindReadableMemoryType(vk::DeviceSize allocation_size,
                                                 uint32_t memory_type_bits);

#ifdef __Fuchsia__
  [[nodiscard]] bool AllocateFuchsiaImportedMemory(zx::vmo exported_memory_vmo);
  [[nodiscard]] bool AssignExportedMemoryHandle();
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
  vk::UniqueDeviceMemory device_memory_;

  // Import/export
  vk::UniqueDeviceMemory imported_device_memory_;
  ImportExport import_export_;
#ifdef __Fuchsia__
  zx::vmo exported_memory_vmo_;
#endif

  vk::UniqueCommandPool command_pool_;
  std::unordered_map<VkReadbackSubmitOptions, vk::UniqueCommandBuffer> command_buffers_;

  VulkanExtensionSupportState timeline_semaphore_support_ =
      VulkanExtensionSupportState::kNotSupported;

  uint64_t bind_offset_ = 0;

  // Submit() validation state.
  bool submit_called_with_transition_ = false;
  bool submit_called_with_barrier_ = false;
};

#endif  // SRC_GRAPHICS_TESTS_VKREADBACK_VKREADBACK_H_
