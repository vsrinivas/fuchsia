// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/forward_declarations.h"
#include "escher/status.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {
class CommandBufferPool;
class GlslToSpirvCompiler;
class GpuAllocator;
class GpuUploader;
class ImageCache;
class MeshManager;
class PipelineCache;
class SsdoSampler;

// Implements the public Escher API.
class EscherImpl {
 public:
  EscherImpl(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~EscherImpl();

  const VulkanContext& vulkan_context();
  CommandBufferPool* command_buffer_pool();
  CommandBufferPool* transfer_command_buffer_pool();
  GpuAllocator* gpu_allocator();
  GpuUploader* gpu_uploader();
  PipelineCache* pipeline_cache();
  ImageCache* image_cache();
  MeshManager* mesh_manager();
  GlslToSpirvCompiler* glsl_compiler();

  void IncrementRendererCount() { ++renderer_count_; }
  void DecrementRendererCount() { --renderer_count_; }

  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

 private:
  VulkanContext vulkan_context_;
  std::unique_ptr<CommandBufferPool> command_buffer_pool_;
  std::unique_ptr<CommandBufferPool> transfer_command_buffer_pool_;
  std::unique_ptr<GpuAllocator> gpu_allocator_;
  std::unique_ptr<GpuUploader> gpu_uploader_;
  std::unique_ptr<PipelineCache> pipeline_cache_;
  std::unique_ptr<ImageCache> image_cache_;
  std::unique_ptr<MeshManager> mesh_manager_;
  std::unique_ptr<GlslToSpirvCompiler> glsl_compiler_;

  std::atomic<uint32_t> renderer_count_;
  std::atomic<uint32_t> resource_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EscherImpl);
};

}  // namespace impl
}  // namespace escher
