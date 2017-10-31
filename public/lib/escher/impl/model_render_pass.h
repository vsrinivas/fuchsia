// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/vk/render_pass.h"

#include "lib/escher/impl/model_pipeline_cache.h"

namespace escher {
namespace impl {

class ModelRenderPass;
typedef fxl::RefPtr<ModelRenderPass> ModelRenderPassPtr;

// Represents a render pass that is used by ModelRenderer.
// Caches the pipelines that are used to render that pass (for example, a model
// object should be renderer with a different pipeline when rendering a shadow
// map).
class ModelRenderPass : public RenderPass {
 public:
  static constexpr uint32_t kColorAttachmentIndex = 0;
  static constexpr uint32_t kDepthAttachmentIndex = 1;

  // Return true of objects rendered using this render-pass should use material
  // textures or not.
  // TODO: look into whether there is a more elegant way to do this.
  virtual bool UseMaterialTextures() = 0;

  // Return true if objects rendered in this render-pass should use a pipeline
  // without a fragment shader.
  virtual bool OmitFragmentShader() = 0;

  uint32_t sample_count() const { return sample_count_; }

  vk::Format color_format() const {
    return attachment(kColorAttachmentIndex)->format;
  }

  vk::Format depth_format() const {
    return attachment(kDepthAttachmentIndex)->format;
  }

  ModelPipelineCache* pipeline_cache() const { return pipeline_cache_.get(); }

 protected:
  ModelRenderPass(ResourceRecycler* recycler,
                  vk::Format color_format,
                  vk::Format depth_format,
                  uint32_t sample_count);

  void CreateRenderPassAndPipelineCache(ModelDataPtr model_data);

 private:
  uint32_t sample_count_;
  ModelPipelineCachePtr pipeline_cache_;
};

}  // namespace impl
}  // namespace escher
