// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vulkan/vulkan.hpp>

#include "escher/forward_declarations.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_display_list.h"
#include "escher/impl/model_display_list_flags.h"
#include "escher/impl/model_pipeline_spec.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"

namespace escher {
namespace impl {

class ModelDisplayListBuilder {
 public:
  // OK to pass null |illumination_texture|; in that case, |white_texture| will
  // be used instead.
  ModelDisplayListBuilder(vk::Device device,
                          const Stage& stage,
                          const Model& model,
                          const Camera& camera,
                          float scale,
                          const TexturePtr& white_texture,
                          const TexturePtr& illumination_texture,
                          ModelData* model_data,
                          ModelRenderer* renderer,
                          ModelPipelineCache* pipeline_cache,
                          ModelDisplayListFlags flags,
                          uint32_t sample_count);

  ~ModelDisplayListBuilder();

  void AddObject(const Object& object);

  ModelDisplayListPtr Build(CommandBuffer* command_buffer);

 private:
  // Called by AddObject() when the object has clippees.  First draws the object
  // and any additional clippers, updating the stencil buffer.  Then, calls
  // AddObject() each of the clippees (note: this may be recursive, since each
  // clippee may be a clipper of its own list of clippees).  Finally, the
  // clippers are redrawn to return the stencil buffer to its original state.
  void AddClipperAndClippeeObjects(const Object& object);
  // Leaf helper called by AddClipperAndClippeeObjects(); actually writes data
  // to uniform buffers, updates descriptor sets, and adds an item to the
  // display list.
  void AddClipperObject(const Object& object);
  // Leaf helper called by AddObject(); actually writes data to uniform buffers,
  // updates descriptor sets, and adds an item to the display list.
  void AddNonClipperObject(const Object& object);

  void PrepareUniformBufferForWriteOfSize(size_t size, size_t alignment);
  vk::DescriptorSet ObtainPerObjectDescriptorSet();
  void UpdateDescriptorSetForObject(const Object& object,
                                    vk::DescriptorSet descriptor_set);

  const vk::Device device_;

  const ViewingVolume volume_;

  // Global camera view/projection matrix, adjusted to meet the needs of this
  // particular display list.
  const mat4 camera_transform_;

  // If this is false, use |default_white_texture_| instead of a material's
  // existing texture (e.g. to save bandwidth during depth-only passes).
  const bool use_material_textures_;

  // If this is true, entirely disable all depth-testing.
  const bool disable_depth_test_;

  const TexturePtr white_texture_;
  const TexturePtr illumination_texture_;

  const vk::DescriptorSet per_model_descriptor_set_;

  std::vector<ModelDisplayList::Item> items_;

  // Textures are handled differently from other resources, because they may
  // have a semaphore that must be waited upon.
  std::vector<TexturePtr> textures_;

  // Uniform buffers are handled differently from other resources, because they
  // must be flushed before they can be used by a display list.
  std::vector<BufferPtr> uniform_buffers_;

  // A list of resources that must be retained until the display list is no
  // longer needed.
  std::vector<ResourcePtr> resources_;

  ModelRenderer* const renderer_;
  UniformBufferPool* const uniform_buffer_pool_;
  DescriptorSetPool* const per_model_descriptor_set_pool_;
  DescriptorSetPool* const per_object_descriptor_set_pool_;
  ModelPipelineCache* const pipeline_cache_;

  DescriptorSetAllocationPtr per_object_descriptor_set_allocation_;

  BufferPtr uniform_buffer_;
  uint32_t uniform_buffer_write_index_ = 0;
  uint32_t per_object_descriptor_set_index_ = 0;

  ModelPipelineSpec pipeline_spec_;
  uint32_t clip_depth_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModelDisplayListBuilder);
};

}  // namespace impl
}  // namespace escher
