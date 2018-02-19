// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/model_render_pass.h"

namespace escher {
namespace impl {

// ModelShadowMapPass encapsulates a vk::RenderPass that is configured to render
// a shadow map.
class ModelShadowMapPass : public ModelRenderPass {
 public:
  ModelShadowMapPass(ResourceRecycler* recycler,
                     ModelDataPtr model_data,
                     vk::Format color_format,
                     vk::Format depth_format,
                     uint32_t sample_count);

 protected:
  // |ModelRenderPass|
  std::string GetVertexShaderMainSourceCode() final;

  // |ModelRenderPass|
  std::string GetFragmentShaderSourceCode(
      const ModelPipelineSpec& spec) override;

  // |ModelRenderPass|
  bool UseMaterialTextures() final { return true; }

  // |ModelRenderPass|
  bool OmitFragmentShader() final { return false; }
};

}  // namespace impl
}  // namespace escher
