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
class GpuAllocator;
class ImageCache;
class MeshManager;
class RenderPassManager;

// Implements the public Escher API.
class EscherImpl {
 public:
  EscherImpl(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~EscherImpl();

  CommandBufferPool* command_buffer_pool();
  ImageCache* image_cache();
  RenderPassManager* render_pass_manager();
  MeshManager* mesh_manager();
  GpuAllocator* gpu_allocator();
  const VulkanContext& vulkan_context();

  void IncrementRendererCount() { ++renderer_count_; }
  void DecrementRendererCount() { --renderer_count_; }

  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

 private:
  VulkanContext vulkan_context_;
  std::unique_ptr<CommandBufferPool> command_buffer_pool_;
  std::unique_ptr<RenderPassManager> render_pass_manager_;
  std::unique_ptr<GpuAllocator> gpu_allocator_;
  std::unique_ptr<ImageCache> image_cache_;
  std::unique_ptr<MeshManager> mesh_manager_;

  std::atomic<uint32_t> renderer_count_;
  std::atomic<uint32_t> resource_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EscherImpl);
};

}  // namespace impl
}  // namespace escher
