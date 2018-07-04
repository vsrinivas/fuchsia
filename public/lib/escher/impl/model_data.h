// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_DATA_H_
#define LIB_ESCHER_IMPL_MODEL_DATA_H_

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "lib/escher/geometry/types.h"
#include "lib/escher/impl/descriptor_set_pool.h"
#include "lib/escher/impl/uniform_buffer_pool.h"
#include "lib/escher/shape/modifier_wobble.h"
#include "lib/escher/util/hash_map.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"

namespace escher {
namespace impl {

class CommandBuffer;
class ModelUniformWriter;

class ModelData : public fxl::RefCountedThreadSafe<ModelData> {
 public:
  // Vertex attribute locations corresponding to the flags in MeshSpec.
  static constexpr uint32_t kPositionAttributeLocation = 0;
  static constexpr uint32_t kPositionOffsetAttributeLocation = 1;
  static constexpr uint32_t kUVAttributeLocation = 2;
  static constexpr uint32_t kPerimeterPosAttributeLocation = 3;

  // Describes per-model data accessible by shaders.
  struct PerModel {
    // Two uniform descriptor, and one texture descriptor.
    // second uniform descriptor is used to hold the ViewProjection
    static constexpr uint32_t kDescriptorCount = 3;
    // layout(set = 0, ...)
    static constexpr uint32_t kDescriptorSetIndex = 0;
    // layout(set = 0, binding = 0) uniform PerModel { ... }
    static constexpr uint32_t kDescriptorSetUniformBinding = 0;
    // layout(set = 0, binding = 1) sampler2D PerModelSampler;
    static constexpr uint32_t kDescriptorSetSamplerBinding = 1;

    // Used by the lighting-pass fragment shader to map fragment coordinates to
    // UV coordinates for the SSDO lighting texture.
    vec2 frag_coord_to_uv_multiplier;
    // Used for animation in vertex shaders.
    float time;
    float __pad1;  // std140

    // Intensities of direct and ambient light sources.
    vec3 ambient_light_intensity;
    float __pad2;  // std140
    vec3 direct_light_intensity;
    float __pad3;  // std140

    // Inverse size of the shadow map texture.
    vec2 shadow_map_uv_multiplier;
  };

  // The VP matrix is put into its own binding in the PerModel DescriptorSet
  // in order to allow it to be bound to a separate buffer to allow late
  // latching view matrices from a PoseBuffer. For details see
  // garnet/public/lib/escher/hmd/pose_buffer_latching_shader.h
  struct ViewProjection {
    // layout(set = 0, binding = 2) uniform ViewProjection { ... }
    static constexpr uint32_t kDescriptorSetUniformBinding = 2;

    // The premultiplied View and Projection matrix.
    mat4 vp_matrix;
  };

  // Describes per-object data accessible by shaders.
  struct PerObject {
    // One uniform descriptor, and one texture descriptor.
    static constexpr uint32_t kDescriptorCount = 2;
    // layout(set = 1, ...)
    static constexpr uint32_t kDescriptorSetIndex = 1;
    // layout(set = 1, binding = 0) uniform PerObject { ... }
    static constexpr uint32_t kDescriptorSetUniformBinding = 0;
    // layout(set = 1, binding = 1) sampler2D PerObjectSampler;
    static constexpr uint32_t kDescriptorSetSamplerBinding = 1;

    // Model matrix.
    mat4 model_transform;
    // Model-light matrix for shadow mapping.
    mat4 shadow_transform;
    // Color of object.  Applied as filter to object's material, if it has one.
    // Otherwise, treated as a solid color.
    vec4 color;
    // Temporary hack.  Soon, per-object params for shape-modifiers, etc. will
    // only be provided to the pipelines that need them.
    ModifierWobble wobble;
  };

  vk::Device device() { return device_; }

  UniformBufferPool* uniform_buffer_pool() { return &uniform_buffer_pool_; }

  DescriptorSetPool* per_model_descriptor_set_pool() {
    return &per_model_descriptor_set_pool_;
  }

  DescriptorSetPool* per_object_descriptor_set_pool() {
    return &per_object_descriptor_set_pool_;
  }

  vk::DescriptorSetLayout per_model_layout() const {
    return per_model_descriptor_set_pool_.layout();
  }

  vk::DescriptorSetLayout per_object_layout() const {
    return per_object_descriptor_set_pool_.layout();
  }

  const MeshShaderBinding& GetMeshShaderBinding(MeshSpec spec);

 private:
  // If no allocator is provided, Escher's default one will be used.
  explicit ModelData(EscherWeakPtr escher, GpuAllocator* allocator = nullptr);

  ~ModelData();

  // Provide access to statically-allocated layout info for per-model and
  // per-object descriptor-sets.
  static const vk::DescriptorSetLayoutCreateInfo&
  GetPerModelDescriptorSetLayoutCreateInfo();
  static const vk::DescriptorSetLayoutCreateInfo&
  GetPerObjectDescriptorSetLayoutCreateInfo();

  vk::Device device_;
  UniformBufferPool uniform_buffer_pool_;
  DescriptorSetPool per_model_descriptor_set_pool_;
  DescriptorSetPool per_object_descriptor_set_pool_;

  HashMap<MeshSpec, std::unique_ptr<MeshShaderBinding>>
      mesh_shader_binding_cache_;

  FRIEND_MAKE_REF_COUNTED(ModelData);
  FRIEND_REF_COUNTED_THREAD_SAFE(ModelData);
  FXL_DISALLOW_COPY_AND_ASSIGN(ModelData);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_DATA_H_
