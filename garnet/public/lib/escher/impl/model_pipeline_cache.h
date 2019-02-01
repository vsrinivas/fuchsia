// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_PIPELINE_CACHE_H_
#define LIB_ESCHER_IMPL_MODEL_PIPELINE_CACHE_H_

#include <memory>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/model_pipeline_spec.h"
#include "lib/escher/resources/resource.h"
#include "lib/escher/util/hash_map.h"
#include "lib/fxl/macros.h"

namespace escher {
namespace impl {

class ModelPipeline;

// ModelPipelineCache supports the retrieval of pipelines that match the
// specified ModelPipelineSpecs, lazily instantiating these pipelines if
// necessary.  The GLSL code required by these specs is hard-coded into the
// implementation.
class ModelPipelineCache : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Ownership of the depth and lighting passes is transferred to the new
  // ModelPipelineCache, which is responsible for destroying them.
  //
  // TODO: Vulkan requires an instantiated render-pass and a specific subpass
  // index within it in order to create a pipeline (as opposed to e.g. Metal,
  // which only requires attachment descriptions).  It somehow feels janky to
  // pass these to the ModelPipelineCache constructor, but what else can we do?
  ModelPipelineCache(ResourceRecycler* recycler, ModelDataPtr model_data,
                     ModelRenderPass* render_pass);
  ~ModelPipelineCache();

  // Get cached pipeline, or return a newly-created one.
  ModelPipeline* GetPipeline(const ModelPipelineSpec& spec);

 private:
  std::unique_ptr<ModelPipeline> NewPipeline(const ModelPipelineSpec& spec);

  ModelDataPtr model_data_;
  ModelRenderPass* const render_pass_;
  HashMap<ModelPipelineSpec, std::unique_ptr<ModelPipeline>> pipelines_;
  std::unique_ptr<GlslToSpirvCompiler> compiler_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ModelPipelineCache);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_PIPELINE_CACHE_H_
