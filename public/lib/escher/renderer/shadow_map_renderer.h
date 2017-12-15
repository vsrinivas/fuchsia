// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/renderer.h"
#include "lib/escher/renderer/shadow_map.h"

namespace escher {

class ShadowMapRenderer;
typedef fxl::RefPtr<ShadowMapRenderer> ShadowMapRendererPtr;

// ShadowMapRenderer is used to render the shadow map corresponding to a
// stage/model, for a specified light type/position/direction/etc.
class ShadowMapRenderer : public Renderer {
 public:
  static const vk::Format kShadowMapFormat = vk::Format::eR16Unorm;

  static ShadowMapRendererPtr New(Escher* escher,
                                  const impl::ModelDataPtr& model_data,
                                  const impl::ModelRendererPtr& model_renderer);

  // Generate a "directional shadow map": one that uses an orthographic
  // projection.  This is used to model very distance light sources (such as the
  // sun), where the light intensity and direction to the light don't change
  // appreciably as an object is moved around the stage.
  ShadowMapPtr GenerateDirectionalShadowMap(const FramePtr& frame,
                                            const Stage& stage,
                                            const Model& model,
                                            const glm::vec3 direction,
                                            const glm::vec3 light_color);

 private:
  ShadowMapRenderer(Escher* escher,
                    const impl::ModelDataPtr& model_data,
                    const impl::ModelRendererPtr& model_renderer);
  ~ShadowMapRenderer() override;

  impl::ImageCache* image_cache_;
  vk::Format depth_format_;
  impl::ModelDataPtr model_data_;
  impl::ModelRendererPtr model_renderer_;
  impl::ModelRenderPassPtr shadow_map_pass_;
  std::vector<vk::ClearValue> clear_values_;

  FRIEND_MAKE_REF_COUNTED(ShadowMapRenderer);
  FRIEND_REF_COUNTED_THREAD_SAFE(ShadowMapRenderer);
  FXL_DISALLOW_COPY_AND_ASSIGN(ShadowMapRenderer);
};

}  // namespace escher
