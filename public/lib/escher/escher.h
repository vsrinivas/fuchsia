// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/shape/mesh_builder_factory.h"
#include "lib/escher/status.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace escher {

// Escher is the primary class used by clients of the Escher library.
//
// Escher is currently not thread-safe; it (and all objects obtained from it)
// must be used from a single thread.
class Escher : public MeshBuilderFactory {
 public:
  // Escher does not take ownership of the objects in the Vulkan context.  It is
  // up to the application to eventually destroy them, and also to ensure that
  // they outlive the Escher instance.
  Escher(VulkanDeviceQueuesPtr device);
  ~Escher();

  // Implement MeshBuilderFactory interface.
  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                size_t max_vertex_count,
                                size_t max_index_count) override;

  // Return new Image containing the provided pixels.
  ImagePtr NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes);
  // Returns RGBA image.
  ImagePtr NewCheckerboardImage(uint32_t width, uint32_t height);
  // Returns RGBA image.
  ImagePtr NewGradientImage(uint32_t width, uint32_t height);
  // Returns single-channel luminance image.
  ImagePtr NewNoiseImage(uint32_t width, uint32_t height);

  // Construct a new Texture, which encapsulates a newly-created VkImageView and
  // VkSampler.  |aspect_mask| is used to create the VkImageView, and |filter|
  // and |use_unnormalized_coordinates| are used to create the VkSampler.
  TexturePtr NewTexture(
      ImagePtr image,
      vk::Filter filter,
      vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor,
      bool use_unnormalized_coordinates = false);

  uint64_t GetNumGpuBytesAllocated();

  VulkanDeviceQueues* device() const { return device_.get(); }
  vk::Device vk_device() const { return device_->vk_device(); }
  vk::PhysicalDevice vk_physical_device() const {
    return device_->vk_physical_device();
  }
  const VulkanContext& vulkan_context() const { return vulkan_context_; }

  ResourceRecycler* resource_recycler() { return resource_recycler_.get(); }
  GpuAllocator* gpu_allocator() { return gpu_allocator_.get(); }
  impl::GpuUploader* gpu_uploader() { return gpu_uploader_.get(); }
  impl::CommandBufferSequencer* command_buffer_sequencer() {
    return command_buffer_sequencer_.get();
  }
  impl::GlslToSpirvCompiler* glsl_compiler() { return glsl_compiler_.get(); }
  impl::ImageCache* image_cache() { return image_cache_.get(); }
  impl::MeshManager* mesh_manager() { return mesh_manager_.get(); }

  // Pool for CommandBuffers submitted on the main queue.
  impl::CommandBufferPool* command_buffer_pool() {
    return command_buffer_pool_.get();
  }
  // Pool for CommandBuffers submitted on the transfer queue (if one exists).
  impl::CommandBufferPool* transfer_command_buffer_pool() {
    return transfer_command_buffer_pool_.get();
  }

 private:
  // Friends that need access to impl_.
  friend class Renderer;
  // friend class ResourceRecycler;
  friend class impl::GpuUploader;
  // Provide access to internal Escher functionality.  Don't use this unless
  // you are on the Escher team: your code will break.
  impl::EscherImpl* impl() const { return impl_.get(); }

  VulkanDeviceQueuesPtr device_;
  VulkanContext vulkan_context_;

  std::unique_ptr<GpuAllocator> gpu_allocator_;
  std::unique_ptr<impl::CommandBufferSequencer> command_buffer_sequencer_;
  std::unique_ptr<impl::CommandBufferPool> command_buffer_pool_;
  std::unique_ptr<impl::CommandBufferPool> transfer_command_buffer_pool_;
  std::unique_ptr<impl::GlslToSpirvCompiler> glsl_compiler_;
  std::unique_ptr<impl::ImageCache> image_cache_;

  std::unique_ptr<impl::GpuUploader> gpu_uploader_;
  std::unique_ptr<ResourceRecycler> resource_recycler_;
  std::unique_ptr<impl::MeshManager> mesh_manager_;

  std::unique_ptr<impl::EscherImpl> impl_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Escher);
};

}  // namespace escher
