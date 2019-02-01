// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_LIGHTING_PASS_H_
#define LIB_ESCHER_IMPL_MODEL_LIGHTING_PASS_H_

#include "lib/escher/impl/model_render_pass.h"

namespace escher {
namespace impl {

// ModelLightingPass encapsulates a vk::RenderPass that is configured for the
// final lighting pass (including shadows, which were computed using SSDO).
class ModelLightingPass : public ModelRenderPass {
 public:
  // |ModelRenderPass|
  bool UseMaterialTextures() override { return true; }

  // |ModelRenderPass|
  bool OmitFragmentShader() override { return false; }

  // |ModelRenderPass|
  std::string GetFragmentShaderSourceCode(
      const ModelPipelineSpec& spec) override;

  ModelLightingPass(ResourceRecycler* recycler, ModelDataPtr model_data,
                    vk::Format color_format, vk::Format depth_format,
                    uint32_t sample_count);

 protected:
  // |ModelRenderPass|
  std::string GetVertexShaderMainSourceCode() override;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_LIGHTING_PASS_H_
