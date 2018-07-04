// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/moment_shadow_map_renderer.h"

#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/model_moment_shadow_map_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/renderer/moment_shadow_map.h"
#include "lib/escher/scene/stage.h"

namespace {

// Create an image of the same size and format as the input.
escher::ImagePtr CreateSimilarImage(escher::Escher* escher,
                                    escher::impl::CommandBuffer* command_buffer,
                                    const escher::ImagePtr& input) {
  escher::ImageInfo info;
  info.format = input->format();
  info.width = input->width();
  info.height = input->height();
  info.sample_count = 1;
  info.usage =
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage;
  auto output = escher->image_cache()->NewImage(info);
  command_buffer->TransitionImageLayout(output, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);
  return output;
}

}  // namespace

namespace escher {

MomentShadowMapRendererPtr MomentShadowMapRenderer::New(
    EscherWeakPtr escher, const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer) {
  auto* resource_recycler = escher->resource_recycler();
  constexpr vk::Format kShadowMapFormat = vk::Format::eR16G16B16A16Sfloat;
  vk::Format depth_format = ESCHER_CHECKED_VK_RESULT(
      impl::GetSupportedDepthStencilFormat(escher->vk_physical_device()));

  return fxl::MakeRefCounted<MomentShadowMapRenderer>(
      std::move(escher), kShadowMapFormat, depth_format, model_data,
      model_renderer,
      fxl::MakeRefCounted<impl::ModelMomentShadowMapPass>(
          resource_recycler, model_data, kShadowMapFormat, depth_format,
          /* sample_count= */ 1));
}

MomentShadowMapRenderer::MomentShadowMapRenderer(
    EscherWeakPtr escher, vk::Format shadow_map_format, vk::Format depth_format,
    const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer,
    const impl::ModelRenderPassPtr& model_render_pass)
    : ShadowMapRenderer(escher, shadow_map_format, depth_format, model_data,
                        model_renderer, model_render_pass),
      gaussian3x3f16_(std::move(escher)) {}

ShadowMapPtr MomentShadowMapRenderer::GenerateDirectionalShadowMap(
    const FramePtr& frame, const Stage& stage, const Model& model,
    const glm::vec3& direction, const glm::vec3& light_color) {
  auto command_buffer = frame->command_buffer();
  auto camera =
      Camera::NewForDirectionalShadowMap(stage.viewing_volume(), direction);
  Stage shadow_stage;
  ComputeShadowStageFromSceneStage(stage, shadow_stage);

  auto width = static_cast<uint32_t>(shadow_stage.width());
  auto height = static_cast<uint32_t>(shadow_stage.height());
  auto color_image = GetTransitionedColorImage(command_buffer, width, height);
  auto depth_image = GetTransitionedDepthImage(command_buffer, width, height);
  DrawShadowPass(command_buffer, shadow_stage, model, camera, color_image,
                 depth_image);
  frame->AddTimestamp("generated moment shadow map");

  command_buffer->TransitionImageLayout(
      color_image, vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::eGeneral);
  frame->AddTimestamp("transitioned layout to eGeneral");

  auto input_texture = fxl::MakeRefCounted<Texture>(
      escher()->resource_recycler(), color_image, vk::Filter::eNearest);
  auto blurred_image =
      CreateSimilarImage(escher(), command_buffer, color_image);
  auto output_texture = fxl::MakeRefCounted<Texture>(
      escher()->resource_recycler(), blurred_image, vk::Filter::eNearest);
  gaussian3x3f16_.Apply(command_buffer, input_texture, output_texture);
  frame->AddTimestamp("applied 3x3 gaussian");

  return SubmitPartialFrameAndBuildShadowMap<MomentShadowMap>(
      frame, camera, blurred_image, light_color);
}

}  // namespace escher
