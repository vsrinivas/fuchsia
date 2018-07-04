// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_SHADOW_MAP_RENDERER_H_
#define LIB_ESCHER_RENDERER_SHADOW_MAP_RENDERER_H_

#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/renderer.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/scene/camera.h"

namespace escher {

class ShadowMapRenderer;
typedef fxl::RefPtr<ShadowMapRenderer> ShadowMapRendererPtr;

// ShadowMapRenderer is used to render the shadow map corresponding to a
// stage/model, for a specified light type/position/direction/etc.
class ShadowMapRenderer : public Renderer {
 public:
  static ShadowMapRendererPtr New(EscherWeakPtr escher,
                                  const impl::ModelDataPtr& model_data,
                                  const impl::ModelRendererPtr& model_renderer);

  // Generate a "directional shadow map": one that uses an orthographic
  // projection.  This is used to model very distance light sources (such as the
  // sun), where the light intensity and direction to the light don't change
  // appreciably as an object is moved around the stage.
  virtual ShadowMapPtr GenerateDirectionalShadowMap(
      const FramePtr& frame, const Stage& stage, const Model& model,
      const glm::vec3& direction, const glm::vec3& light_color);

 protected:
  ShadowMapRenderer(EscherWeakPtr escher, vk::Format shadow_map_format,
                    vk::Format depth_format,
                    const impl::ModelDataPtr& model_data,
                    const impl::ModelRendererPtr& model_renderer,
                    const impl::ModelRenderPassPtr& model_render_pass);
  ~ShadowMapRenderer() override;

  void ComputeShadowStageFromSceneStage(const Stage& scene_stage,
                                        Stage& shadow_stage);

  ImagePtr GetTransitionedColorImage(impl::CommandBuffer* command_buffer,
                                     uint32_t width, uint32_t height);
  ImagePtr GetTransitionedDepthImage(impl::CommandBuffer* command_buffer,
                                     uint32_t width, uint32_t height);
  void DrawShadowPass(impl::CommandBuffer* command_buffer,
                      const Stage& shadow_stage, const Model& model,
                      const Camera& camera, ImagePtr& color_image,
                      ImagePtr& depth_image);

  template <typename ShadowMapT>
  fxl::RefPtr<ShadowMapT> SubmitPartialFrameAndBuildShadowMap(
      const FramePtr& frame, const Camera& camera, ImagePtr& color_image,
      const glm::vec3& light_color) {
    auto semaphore = escher::Semaphore::New(escher()->vk_device());
    frame->SubmitPartialFrame(semaphore);
    color_image->SetWaitSemaphore(std::move(semaphore));

    // NOTE: the bias matrix used for shadowmapping in Vulkan is different than
    // OpenGL, so we can't use glm::scaleBias().
    const mat4 bias(0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                    0.5, 0.5, 0.0, 1.0);
    return fxl::AdoptRef(new ShadowMapT(
        std::move(color_image), bias * camera.projection() * camera.transform(),
        light_color));
  }

 private:
  impl::ImageCache* image_cache_;
  vk::Format shadow_map_format_;
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

#endif  // LIB_ESCHER_RENDERER_SHADOW_MAP_RENDERER_H_
