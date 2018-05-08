// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MODEL_MOMENT_SHADOW_MAP_LIGHTING_PASS_H_
#define LIB_ESCHER_IMPL_MODEL_MOMENT_SHADOW_MAP_LIGHTING_PASS_H_

#include "lib/escher/impl/model_shadow_map_lighting_pass.h"

namespace escher {
namespace impl {

// ModelMomentShadowMapLightingPass encapsulates a ModelShadowMapLightingPass
// that is configured for lighting pass with MomentShadowMap.
// http://momentsingraphics.de/?page_id=51
class ModelMomentShadowMapLightingPass final
    : public ModelShadowMapLightingPass {
 public:
  ModelMomentShadowMapLightingPass(ResourceRecycler* recycler,
                                   ModelDataPtr model_data,
                                   vk::Format color_format,
                                   vk::Format depth_format,
                                   uint32_t sample_count);

  // |ModelRenderPass|
  std::string GetFragmentShaderSourceCode(
      const ModelPipelineSpec& spec) override;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MODEL_MOMENT_SHADOW_MAP_LIGHTING_PASS_H_
