// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "escher/forward_declarations.h"
#include "escher/impl/glsl_compiler.h"
#include "escher/impl/model_pipeline_spec.h"
#include "ftl/macros.h"

namespace escher {
namespace impl {

struct MeshSpecImpl;
class ModelPipeline;

class ModelPipelineCache {
 public:
  // TODO: Vulkan requires an instantiated render-pass and a specific subpass
  // index within it in order to create a pipeline (as opposed to e.g. Metal,
  // which only requires attachment descriptions).  It somehow feels janky to
  // pass these to the ModelPipelineCache constructor.
  ModelPipelineCache(vk::Device device,
                     vk::RenderPass render_pass,
                     uint32_t subpass_index,
                     ModelData* model_data);
  ~ModelPipelineCache();

  // The MeshSpecImpl is used in case a new pipeline needs to be created.
  // TODO: passing the MeshSpecImpl is kludgy, especially since the caller may
  // need to look it up in a cache.
  ModelPipeline* GetPipeline(const ModelPipelineSpec& spec,
                             const MeshSpecImpl& mesh_spec_impl);

 private:
  std::unique_ptr<ModelPipeline> NewPipeline(
      const ModelPipelineSpec& spec,
      const MeshSpecImpl& mesh_spec_impl);

  vk::Device device_;
  vk::RenderPass render_pass_;
  uint32_t subpass_index_;
  ModelData* model_data_;
  std::unordered_map<ModelPipelineSpec,
                     std::unique_ptr<ModelPipeline>,
                     ModelPipelineSpec::Hash>
      pipelines_;
  GlslToSpirvCompiler compiler_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelPipelineCache);
};

}  // namespace impl
}  // namespace escher
