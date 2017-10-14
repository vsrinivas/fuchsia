// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/escher_impl.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/impl/vk/pipeline_cache.h"
#include "lib/escher/profiling/timestamp_profiler.h"

namespace escher {
namespace impl {

namespace {

// Constructor helper.
std::unique_ptr<MeshManager> NewMeshManager(
    CommandBufferPool* main_pool,
    CommandBufferPool* transfer_pool,
    GpuAllocator* allocator,
    GpuUploader* uploader,
    ResourceRecycler* resource_recycler) {
  return std::make_unique<MeshManager>(
      transfer_pool ? transfer_pool : main_pool, allocator, uploader,
      resource_recycler);
}

}  // namespace

EscherImpl::EscherImpl(Escher* escher, const VulkanContext& context)
    : escher_(escher),
      vulkan_context_(context),
      pipeline_cache_(std::make_unique<PipelineCache>()),
      mesh_manager_(NewMeshManager(escher->command_buffer_pool(),
                                   escher->transfer_command_buffer_pool(),
                                   escher->gpu_allocator(),
                                   escher->gpu_uploader(),
                                   escher->resource_recycler())),
      renderer_count_(0) {
  FXL_DCHECK(context.instance);
  FXL_DCHECK(context.physical_device);
  FXL_DCHECK(context.device);
  FXL_DCHECK(context.queue);
  // TODO: additional validation, e.g. ensure that queue supports both graphics
  // and compute.

  auto device_properties = context.physical_device.getProperties();
  timestamp_period_ = device_properties.limits.timestampPeriod;
  auto queue_properties =
      context.physical_device
          .getQueueFamilyProperties()[context.queue_family_index];
  supports_timer_queries_ = queue_properties.timestampValidBits > 0;
}

EscherImpl::~EscherImpl() {
  FXL_DCHECK(renderer_count_ == 0);

  vulkan_context_.device.waitIdle();

  Cleanup();
}

void EscherImpl::Cleanup() {
  command_buffer_pool()->Cleanup();
  if (auto pool = transfer_command_buffer_pool()) {
    pool->Cleanup();
  }
}

const VulkanContext& EscherImpl::vulkan_context() {
  return escher_->vulkan_context();
}

CommandBufferSequencer* EscherImpl::command_buffer_sequencer() {
  return escher_->command_buffer_sequencer();
}

CommandBufferPool* EscherImpl::command_buffer_pool() {
  return escher_->command_buffer_pool();
}

CommandBufferPool* EscherImpl::transfer_command_buffer_pool() {
  return escher_->transfer_command_buffer_pool();
}

ImageCache* EscherImpl::image_cache() {
  return escher_->image_cache();
}

MeshManager* EscherImpl::mesh_manager() {
  return mesh_manager_.get();
}

GlslToSpirvCompiler* EscherImpl::glsl_compiler() {
  return escher_->glsl_compiler();
}

ResourceRecycler* EscherImpl::resource_recycler() {
  return escher_->resource_recycler();
}

GpuAllocator* EscherImpl::gpu_allocator() {
  return escher_->gpu_allocator();
}

GpuUploader* EscherImpl::gpu_uploader() {
  return escher_->gpu_uploader();
}

}  // namespace impl
}  // namespace escher
