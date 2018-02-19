// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/model_shadow_map_pass.h"

namespace escher {
namespace impl {

// ModelMomentShadowMapPass encapsulates a ModelShadowMapPass that is configured
// to render a moment shadow map.
class ModelMomentShadowMapPass final : public ModelShadowMapPass {
 public:
  ModelMomentShadowMapPass(ResourceRecycler* recycler,
                           const ModelDataPtr& model_data,
                           vk::Format color_format,
                           vk::Format depth_format,
                           uint32_t sample_count);
 protected:
  // |ModelRenderPass|
  std::string GetFragmentShaderSourceCode(
      const ModelPipelineSpec& spec) override;
};

}  // namespace impl
}  // namespace escher
