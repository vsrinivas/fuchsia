// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <map>
#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "escher/impl/descriptor_set_pool.h"
#include "escher/impl/uniform_buffer_pool.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class CommandBuffer;
class ModelUniformWriter;
class GpuAllocator;

class ModelData {
 public:
  // Describes per-model data accessible by shaders.
  struct PerModel {
    // layout(set = 0, ...)
    static constexpr uint32_t kDescriptorSetIndex = 0;
    // layout(set = 0, binding = 0) uniform PerObject { ... }
    static constexpr uint32_t kDescriptorSetUniformBinding = 0;

    vec4 brightness;
  };

  // Describes per-object data accessible by shaders.
  struct PerObject {
    // layout(set = 1, ...)
    static constexpr uint32_t kDescriptorSetIndex = 1;
    // layout(set = 1, binding = 0) uniform PerObject { ... }
    static constexpr uint32_t kDescriptorSetUniformBinding = 0;
    // layout(set = 1, binding = 1) sampler2D PerObjectSampler;
    static constexpr uint32_t kDescriptorSetSamplerBinding = 1;

    mat4 transform;
    vec4 color;
  };

  struct PerVertex {
    vec2 position;
    vec2 uv;
  };

  ModelData(vk::Device device, GpuAllocator* allocator);
  ~ModelData();

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

 private:
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

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelData);
};

}  // namespace impl
}  // namespace escher
