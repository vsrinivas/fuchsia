// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/moment_shadow_map_renderer.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/model_moment_shadow_map_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/renderer/moment_shadow_map.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/stage.h"

namespace escher {

MomentShadowMapRendererPtr MomentShadowMapRenderer::New(
    Escher* escher,
    const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer) {
  constexpr vk::Format kShadowMapFormat = vk::Format::eR32G32B32A32Sfloat;
  vk::Format depth_format = ESCHER_CHECKED_VK_RESULT(
      impl::GetSupportedDepthStencilFormat(escher->vk_physical_device()));
  return fxl::MakeRefCounted<MomentShadowMapRenderer>(
      escher, kShadowMapFormat, depth_format, model_data, model_renderer,
      fxl::MakeRefCounted<impl::ModelMomentShadowMapPass>(
          escher->resource_recycler(),
          model_data,
          kShadowMapFormat,
          depth_format,
          /* sample_count= */ 1));
}

MomentShadowMapRenderer::MomentShadowMapRenderer(
    Escher* escher,
    vk::Format shadow_map_format,
    vk::Format depth_format,
    const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer,
    const impl::ModelRenderPassPtr& model_render_pass)
    : ShadowMapRenderer(escher,
                        shadow_map_format,
                        depth_format,
                        model_data,
                        model_renderer,
                        model_render_pass) {}

ShadowMapPtr MomentShadowMapRenderer::GenerateDirectionalShadowMap(
    const FramePtr& frame,
    const Stage& stage,
    const Model& model,
    const glm::vec3& direction,
    const glm::vec3& light_color) {
  auto command_buffer = frame->command_buffer();
  auto camera =
      Camera::NewForDirectionalShadowMap(stage.viewing_volume(), direction);
  Stage shadow_stage;
  ComputeShadowStageFromSceneStage(stage, shadow_stage);

  auto width = static_cast<uint32_t>(shadow_stage.width());
  auto height = static_cast<uint32_t>(shadow_stage.height());
  auto color_image = GetTransitionedColorImage(command_buffer, width, height);
  auto depth_image = GetTransitionedDepthImage(command_buffer, width, height);
  DrawShadowPass(
      command_buffer, shadow_stage, model, camera, color_image, depth_image);
  frame->AddTimestamp("generated moment shadow map");
  // TODO(ES-67): Apply Gaussian blur on color_image.
  return SubmitPartialFrameAndBuildShadowMap<MomentShadowMap>(
      frame, camera, color_image, light_color);
}

}  // namespace escher
