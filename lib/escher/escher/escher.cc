// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/escher.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/image_cache.h"
#include "escher/impl/mesh_manager.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/renderer/texture.h"
#include "escher/resources/resource_recycler.h"
#include "escher/util/image_utils.h"
#include "escher/vk/gpu_allocator.h"

namespace escher {

Escher::Escher(const VulkanContext& context, VulkanDeviceQueuesPtr device)
    : device_(std::move(device)),
      impl_(std::make_unique<impl::EscherImpl>(this, context)) {}

Escher::~Escher() {}

MeshBuilderPtr Escher::NewMeshBuilder(const MeshSpec& spec,
                                      size_t max_vertex_count,
                                      size_t max_index_count) {
  return impl_->mesh_manager()->NewMeshBuilder(spec, max_vertex_count,
                                               max_index_count);
}

ImagePtr Escher::NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes) {
  return image_utils::NewRgbaImage(impl_->image_cache(), impl_->gpu_uploader(),
                                   width, height, bytes);
}

ImagePtr Escher::NewCheckerboardImage(uint32_t width, uint32_t height) {
  return image_utils::NewCheckerboardImage(
      impl_->image_cache(), impl_->gpu_uploader(), width, height);
}

ImagePtr Escher::NewGradientImage(uint32_t width, uint32_t height) {
  return image_utils::NewGradientImage(impl_->image_cache(),
                                       impl_->gpu_uploader(), width, height);
}

ImagePtr Escher::NewNoiseImage(uint32_t width, uint32_t height) {
  return image_utils::NewNoiseImage(impl_->image_cache(), impl_->gpu_uploader(),
                                    width, height);
}

PaperRendererPtr Escher::NewPaperRenderer() {
  auto renderer = new PaperRenderer(impl_.get());
  return ftl::AdoptRef(renderer);
}

TexturePtr Escher::NewTexture(ImagePtr image,
                              vk::Filter filter,
                              vk::ImageAspectFlags aspect_mask,
                              bool use_unnormalized_coordinates) {
  return ftl::MakeRefCounted<Texture>(impl_->resource_recycler(),
                                      std::move(image), filter, aspect_mask,
                                      use_unnormalized_coordinates);
}

uint64_t Escher::GetNumGpuBytesAllocated() {
  return impl_->gpu_allocator()->total_slab_bytes();
}

const VulkanContext& Escher::vulkan_context() {
  return impl_->vulkan_context();
}

ResourceRecycler* Escher::resource_recycler() {
  return impl_->resource_recycler();
}

GpuAllocator* Escher::gpu_allocator() {
  return impl_->gpu_allocator();
}

impl::GpuUploader* Escher::gpu_uploader() {
  return impl_->gpu_uploader();
}

impl::CommandBufferSequencer* Escher::command_buffer_sequencer() {
  return impl_->command_buffer_sequencer();
}

impl::CommandBufferPool* Escher::command_buffer_pool() {
  return impl_->command_buffer_pool();
}

}  // namespace escher
