// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/status.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/fxl/macros.h"

namespace escher {
class Escher;

namespace impl {
class CommandBufferSequencer;
class CommandBufferPool;
class GlslToSpirvCompiler;
class GpuUploader;
class ImageCache;
class MeshManager;
class PipelineCache;
class SsdoSampler;

// Implements the public Escher API.
class EscherImpl {
 public:
  EscherImpl(Escher* escher, const VulkanContext& context);
  ~EscherImpl();

  Escher* escher() { return escher_; }
  const VulkanContext& vulkan_context();
  CommandBufferSequencer* command_buffer_sequencer();
  CommandBufferPool* command_buffer_pool();
  CommandBufferPool* transfer_command_buffer_pool();
  GpuAllocator* gpu_allocator();
  GpuUploader* gpu_uploader();
  PipelineCache* pipeline_cache();
  ImageCache* image_cache();
  MeshManager* mesh_manager();
  GlslToSpirvCompiler* glsl_compiler();
  ResourceRecycler* resource_recycler();

  bool supports_timer_queries() const { return supports_timer_queries_; }
  float timestamp_period() const { return timestamp_period_; }

  void IncrementRendererCount() { ++renderer_count_; }
  void DecrementRendererCount() { --renderer_count_; }

  void IncrementResourceCount() { ++resource_count_; }
  void DecrementResourceCount() { --resource_count_; }

  // Do periodic housekeeping.
  void Cleanup();

 private:
  Escher* const escher_;
  VulkanContext vulkan_context_;

  std::unique_ptr<PipelineCache> pipeline_cache_;

  std::atomic<uint32_t> renderer_count_;
  std::atomic<uint32_t> resource_count_;

  bool supports_timer_queries_ = false;
  float timestamp_period_ = 0.f;

  FXL_DISALLOW_COPY_AND_ASSIGN(EscherImpl);
};

}  // namespace impl
}  // namespace escher
