// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "escher/forward_declarations.h"
#include "escher/shape/mesh_builder_factory.h"
#include "escher/status.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/macros.h"
#include "ftl/memory/ref_ptr.h"

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
  Escher(const VulkanContext& context);
  ~Escher();

  // Implement MeshBuilderFactory interface.
  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                size_t max_vertex_count,
                                size_t max_index_count) override;

  // Return new Image containing the provided pixels.
  ImagePtr NewRgbaImage(uint32_t width, uint32_t height, uint8_t* bytes);
  // Returns RGBA image.
  ImagePtr NewCheckerboardImage(uint32_t width, uint32_t height);
  // Returns single-channel luminance image.
  ImagePtr NewNoiseImage(uint32_t width, uint32_t height);

  PaperRendererPtr NewPaperRenderer();

  // Construct a new Texture, which encapsulates a newly-created VkImageView and
  // VkSampler.  |aspect_mask| is used to create the VkImageView, and |filter|
  // and |use_unnormalized_coordinates| are used to create the VkSampler.
  TexturePtr NewTexture(
      ImagePtr image,
      vk::Filter filter,
      vk::ImageAspectFlags aspect_mask = vk::ImageAspectFlagBits::eColor,
      bool use_unnormalized_coordinates = false);

  uint64_t GetNumGpuBytesAllocated();

  const VulkanContext& vulkan_context();
  ResourceRecycler* resource_recycler();
  GpuAllocator* gpu_allocator();
  impl::GpuUploader* gpu_uploader();

 private:
  std::unique_ptr<impl::EscherImpl> impl_;

  FTL_DISALLOW_COPY_AND_ASSIGN(Escher);
};

}  // namespace escher
