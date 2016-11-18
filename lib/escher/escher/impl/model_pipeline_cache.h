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

// TODO: document.
class ModelPipelineCache {
 public:
  ModelPipelineCache(vk::Device device,
                     vk::RenderPass depth_prepass,
                     vk::RenderPass lighting_pass);
  virtual ~ModelPipelineCache() {}

  // The MeshSpecImpl is used in case a new pipeline needs to be created.
  // TODO: passing the MeshSpecImpl is kludgy, especially since the caller may
  // need to look it up in a cache.
  virtual ModelPipeline* GetPipeline(const ModelPipelineSpec& spec,
                                     const MeshSpecImpl& mesh_spec_impl) = 0;

 protected:
  vk::Device device_;
  vk::RenderPass depth_prepass_;
  vk::RenderPass lighting_pass_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ModelPipelineCache);
};

// Work in progress, will be killed soon.
class ModelPipelineCacheOLD : public ModelPipelineCache {
 public:
  // TODO: Vulkan requires an instantiated render-pass and a specific subpass
  // index within it in order to create a pipeline (as opposed to e.g. Metal,
  // which only requires attachment descriptions).  It somehow feels janky to
  // pass these to the ModelPipelineCache constructor.
  ModelPipelineCacheOLD(vk::Device device,
                        vk::RenderPass depth_prepass,
                        vk::RenderPass lighting_pass,
                        ModelData* model_data);
  ~ModelPipelineCacheOLD();

  // The MeshSpecImpl is used in case a new pipeline needs to be created.
  // TODO: passing the MeshSpecImpl is kludgy, especially since the caller may
  // need to look it up in a cache.
  ModelPipeline* GetPipeline(const ModelPipelineSpec& spec,
                             const MeshSpecImpl& mesh_spec_impl) override;

  GlslToSpirvCompiler* glsl_compiler() { return &compiler_; }

 private:
  std::unique_ptr<ModelPipeline> NewPipeline(
      const ModelPipelineSpec& spec,
      const MeshSpecImpl& mesh_spec_impl);

  ModelData* model_data_;
  std::unordered_map<ModelPipelineSpec,
                     std::unique_ptr<ModelPipeline>,
                     ModelPipelineSpec::Hash>
      pipelines_;
  GlslToSpirvCompiler compiler_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ModelPipelineCacheOLD);
};

}  // namespace impl
}  // namespace escher
