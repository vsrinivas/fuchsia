// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <map>
#include <vulkan/vulkan.hpp>

#include "escher/geometry/types.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

class ModelUniformWriter;
class GpuAllocator;
class RenderFrame;

class ModelData {
 public:
  struct PerModel {
    static constexpr uint32_t kDescriptorSetBindIndex = 0;

    vec4 brightness;
  };

  struct PerObject {
    static constexpr uint32_t kDescriptorSetBindIndex = 1;

    mat4 transform;
    vec4 color;
  };

  struct ColorVertex {
    vec2 position;
    vec3 color;
  };

  ModelData(vk::Device device, GpuAllocator* allocator);
  ~ModelData();

  // Return a writer that has enough capacity to write the specified number of
  // PerObject structs (and a single PerModel struct).
  ModelUniformWriter* GetWriterWithCapacity(RenderFrame* frame,
                                            size_t max_object_count,
                                            float overallocate_percent);

  vk::DescriptorSetLayout per_model_layout() const { return per_model_layout_; }
  vk::DescriptorSetLayout per_object_layout() const {
    return per_object_layout_;
  }

 private:
  vk::DescriptorSetLayout NewPerModelLayout();
  vk::DescriptorSetLayout NewPerObjectLayout();

  vk::Device device_;
  GpuAllocator* allocator_;

  vk::DescriptorSetLayout per_model_layout_;
  vk::DescriptorSetLayout per_object_layout_;

  // We associate a ModelUniformWriter with a particular RenderFrame.  This
  // piggyback's on the reuse pattern established by Renderer/RenderFrame: a
  // particular RenderFrame doesn't become available for reuse until all of its
  // associated queue submissions have finished.  Therefore, if we see that a
  // RenderFrame is being reused, we know that it's also safe to reuse the
  // corresponding ModelUniformWriter.
  // TODO: two possible alternatives:
  //  1) Stash the writers as instance variables in the RenderFrame.  This is a
  //     step toward bloating the RenderFrame, as new renderer sub-systems each
  //     add their idiosyncratic data.
  //  2) Add a callback mechanism to be notified when a RenderFrame is
  //     successfully retired.  Compared with the current implementation, one
  //     benefit would be that there is no danger of "leaking" writers.  For
  //     example, if the Renderer implementation were to change to not recycle
  //     RenderFrames, then each call to GetWriterWithCapacity() would result
  //     in a new writer being created, and no writer ever destroyed.
  std::map<RenderFrame*, std::unique_ptr<ModelUniformWriter>> writers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelData);
};

}  // namespace impl
}  // namespace escher
