// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "escher/forward_declarations.h"
#include "escher/impl/glsl_compiler.h"
#include "escher/impl/model_pipeline_spec.h"
#include "escher/util/hash.h"
#include "lib/fxl/macros.h"

namespace escher {
namespace impl {

class ModelPipeline;

// TODO: document.
class ModelPipelineCache {
 public:
  // TODO: Vulkan requires an instantiated render-pass and a specific subpass
  // index within it in order to create a pipeline (as opposed to e.g. Metal,
  // which only requires attachment descriptions).  It somehow feels janky to
  // pass these to the ModelPipelineCache constructor, but what else can we do?
  ModelPipelineCache(ModelData* model_data,
                     vk::RenderPass depth_prepass,
                     vk::RenderPass lighting_pass);
  ~ModelPipelineCache();

  // Get cached pipeline, or return a newly-created one.
  ModelPipeline* GetPipeline(const ModelPipelineSpec& spec);

  GlslToSpirvCompiler* glsl_compiler() { return &compiler_; }

 private:
  std::unique_ptr<ModelPipeline> NewPipeline(const ModelPipelineSpec& spec);

  ModelData* const model_data_;
  vk::RenderPass depth_prepass_;
  vk::RenderPass lighting_pass_;
  std::unordered_map<ModelPipelineSpec,
                     std::unique_ptr<ModelPipeline>,
                     Hash<ModelPipelineSpec>>
      pipelines_;
  GlslToSpirvCompiler compiler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModelPipelineCache);
};

}  // namespace impl
}  // namespace escher
