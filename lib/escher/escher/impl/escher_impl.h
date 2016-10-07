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
class GpuAllocator;
class MeshManager;
class RenderContext;

// Implements the public Escher API.
class EscherImpl {
 public:
  EscherImpl(const VulkanContext& context, const VulkanSwapchain& swapchain);
  ~EscherImpl();

  // Public API methods.  See escher.h
  Status Render(const Stage& stage, const Model& model);
  void SetSwapchain(const VulkanSwapchain& swapchain);

  MeshManager* GetMeshManager();

  void IncrementRendererCount() { ++renderer_count_; }
  void DecrementRendererCount() { --renderer_count_; }

 private:
  vk::Device device_;
  std::unique_ptr<GpuAllocator> allocator_;
  std::unique_ptr<MeshManager> mesh_manager_;
  std::unique_ptr<RenderContext> render_context_;

  bool device_lost_ = false;

  std::atomic<uint32_t> renderer_count_;

  FTL_DISALLOW_COPY_AND_ASSIGN(EscherImpl);
};

}  // namespace impl
}  // namespace escher
