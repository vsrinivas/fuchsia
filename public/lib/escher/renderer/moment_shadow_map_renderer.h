// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/gaussian_3x3f.h"
#include "lib/escher/renderer/shadow_map_renderer.h"

namespace escher {

class MomentShadowMapRenderer;
typedef fxl::RefPtr<MomentShadowMapRenderer> MomentShadowMapRendererPtr;

class MomentShadowMapRenderer final : public ShadowMapRenderer {
 public:
  static MomentShadowMapRendererPtr New(
      Escher* escher,
      const impl::ModelDataPtr& model_data,
      const impl::ModelRendererPtr& model_renderer);

  // |ShadowMapRenderer|
  ShadowMapPtr GenerateDirectionalShadowMap(
      const FramePtr& frame,
      const Stage& stage,
      const Model& model,
      const glm::vec3& direction,
      const glm::vec3& light_color) override;

 protected:
  MomentShadowMapRenderer(Escher* escher,
                          vk::Format shadow_map_format,
                          vk::Format depth_format,
                          const impl::ModelDataPtr& model_data,
                          const impl::ModelRendererPtr& model_renderer,
                          const impl::ModelRenderPassPtr& model_render_pass);

 private:
  impl::Gaussian3x3f gaussian3x3f_;

  FRIEND_MAKE_REF_COUNTED(MomentShadowMapRenderer);
  FRIEND_REF_COUNTED_THREAD_SAFE(MomentShadowMapRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(MomentShadowMapRenderer);
};

}  // namespace escher
