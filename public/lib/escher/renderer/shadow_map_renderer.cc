// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/shadow_map_renderer.h"

#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list.h"
#include "lib/escher/impl/model_renderer.h"
#include "lib/escher/impl/model_shadow_map_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/math/rotations.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

ShadowMapRendererPtr ShadowMapRenderer::New(
    EscherWeakPtr escher, const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer) {
  auto* resource_recycler = escher->resource_recycler();
  constexpr vk::Format kShadowMapFormat = vk::Format::eR16Unorm;
  vk::Format depth_format = ESCHER_CHECKED_VK_RESULT(
      impl::GetSupportedDepthStencilFormat(escher->vk_physical_device()));
  return fxl::MakeRefCounted<ShadowMapRenderer>(
      std::move(escher), kShadowMapFormat, depth_format, model_data,
      model_renderer,
      fxl::MakeRefCounted<impl::ModelShadowMapPass>(
          resource_recycler, model_data, kShadowMapFormat, depth_format,
          /* sample_count= */ 1));
}

ShadowMapRenderer::ShadowMapRenderer(
    EscherWeakPtr weak_escher, vk::Format shadow_map_format,
    vk::Format depth_format, const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer,
    const impl::ModelRenderPassPtr& model_render_pass)
    : Renderer(std::move(weak_escher)),
      image_cache_(escher()->image_cache()),
      shadow_map_format_(shadow_map_format),
      depth_format_(depth_format),
      model_data_(model_data),
      model_renderer_(model_renderer),
      shadow_map_pass_(model_render_pass),
      clear_values_(
          {vk::ClearColorValue(std::array<float, 4>{{0.f, 0.f, 0.f, 1.f}}),
           vk::ClearDepthStencilValue(1.f, 0.f)}) {}

ShadowMapRenderer::~ShadowMapRenderer() { escher()->Cleanup(); }

ShadowMapPtr ShadowMapRenderer::GenerateDirectionalShadowMap(
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
  frame->AddTimestamp("generated shadow map");
  return SubmitPartialFrameAndBuildShadowMap<ShadowMap>(
      frame, camera, color_image, light_color);
}

ImagePtr ShadowMapRenderer::GetTransitionedColorImage(
    impl::CommandBuffer* command_buffer, uint32_t width, uint32_t height) {
  ImageInfo info;
  info.format = shadow_map_format_;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eTransferSrc |
               vk::ImageUsageFlagBits::eStorage;
  auto color_image = escher()->image_cache()->NewImage(info);
  command_buffer->TransitionImageLayout(
      color_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);
  return color_image;
}

ImagePtr ShadowMapRenderer::GetTransitionedDepthImage(
    impl::CommandBuffer* command_buffer, uint32_t width, uint32_t height) {
  auto depth_image =
      image_utils::NewDepthImage(escher()->image_cache(), depth_format_, width,
                                 height, vk::ImageUsageFlags());
  command_buffer->TransitionImageLayout(
      depth_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);
  return depth_image;
}

void ShadowMapRenderer::DrawShadowPass(impl::CommandBuffer* command_buffer,
                                       const Stage& shadow_stage,
                                       const Model& model, const Camera& camera,
                                       ImagePtr& color_image,
                                       ImagePtr& depth_image) {
  auto fb = fxl::MakeRefCounted<Framebuffer>(escher(), color_image, depth_image,
                                             shadow_map_pass_->vk());
  auto display_list = model_renderer_->CreateDisplayList(
      shadow_stage, model, camera, shadow_map_pass_,
      impl::ModelDisplayListFlag::kNull, 1.f, TexturePtr(), mat4(1.f),
      vec3(0.f), vec3(0.f), command_buffer);

  command_buffer->KeepAlive(fb);
  command_buffer->KeepAlive(display_list);

  command_buffer->BeginRenderPass(
      shadow_map_pass_, fb, clear_values_,
      camera.viewport().vk_rect_2d(fb->width(), fb->height()));
  model_renderer_->Draw(shadow_stage, display_list, command_buffer,
                        camera.viewport());
  command_buffer->EndRenderPass();
}

void ShadowMapRenderer::ComputeShadowStageFromSceneStage(
    const Stage& scene_stage, Stage& shadow_stage) {
  uint32_t shadow_size = static_cast<uint32_t>(
      .75f * std::max(scene_stage.width(), scene_stage.height()));
  shadow_stage.set_viewing_volume(escher::ViewingVolume(
      shadow_size, shadow_size, scene_stage.viewing_volume().top(),
      scene_stage.viewing_volume().bottom()));
  shadow_stage.set_key_light(scene_stage.key_light());
  shadow_stage.set_fill_light(scene_stage.fill_light());
}

}  // namespace escher
