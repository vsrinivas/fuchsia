// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/shadow_map_renderer.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_display_list.h"
#include "lib/escher/impl/model_renderer.h"
#include "lib/escher/impl/model_shadow_map_pass.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/math/rotations.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/stage.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

const vk::Format ShadowMapRenderer::kShadowMapFormat;

ShadowMapRendererPtr ShadowMapRenderer::New(
    Escher* escher,
    const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer) {
  return fxl::MakeRefCounted<ShadowMapRenderer>(escher, model_data,
                                                model_renderer);
}

ShadowMapRenderer::ShadowMapRenderer(
    Escher* escher,
    const impl::ModelDataPtr& model_data,
    const impl::ModelRendererPtr& model_renderer)
    : Renderer(escher),
      image_cache_(escher->image_cache()),
      depth_format_(ESCHER_CHECKED_VK_RESULT(
          impl::GetSupportedDepthStencilFormat(escher->vk_physical_device()))),
      model_data_(model_data),
      model_renderer_(model_renderer),
      shadow_map_pass_(fxl::MakeRefCounted<impl::ModelShadowMapPass>(
          escher->resource_recycler(),
          model_data_,
          kShadowMapFormat,
          depth_format_,
          1)),
      clear_values_(
          {vk::ClearColorValue(std::array<float, 4>{{0.f, 0.f, 0.f, 1.f}}),
           vk::ClearDepthStencilValue(1.f, 0.f)}) {}

ShadowMapRenderer::~ShadowMapRenderer() {
  escher()->Cleanup();
}

ShadowMapPtr ShadowMapRenderer::GenerateDirectionalShadowMap(
    const Stage& stage,
    const Model& model,
    const glm::vec3 direction,
    const glm::vec3 light_color) {
  auto camera =
      Camera::NewForDirectionalShadowMap(stage.viewing_volume(), direction);

  ImageInfo info;
  info.format = kShadowMapFormat;
  info.width = static_cast<uint32_t>(stage.width());
  info.height = static_cast<uint32_t>(stage.height());
  info.sample_count = 1;
  info.usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eTransferSrc;

  auto color_image = escher()->image_cache()->NewImage(info);
  auto depth_image = image_utils::NewDepthImage(
      escher()->image_cache(), depth_format_, stage.width(), stage.height(),
      vk::ImageUsageFlags());

  auto fb = fxl::MakeRefCounted<Framebuffer>(escher(), color_image, depth_image,
                                             shadow_map_pass_->vk());

  BeginFrame();
  auto command_buffer = current_frame();

  command_buffer->TransitionImageLayout(
      color_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);
  command_buffer->TransitionImageLayout(
      depth_image, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

  auto display_list = model_renderer_->CreateDisplayList(
      stage, model, camera, shadow_map_pass_, impl::ModelDisplayListFlag::kNull,
      1.f, TexturePtr(), mat4(1.f), vec3(0.f), vec3(0.f), command_buffer);

  command_buffer->KeepAlive(fb);
  command_buffer->KeepAlive(display_list);

  command_buffer->BeginRenderPass(shadow_map_pass_, fb, clear_values_);
  model_renderer_->Draw(stage, display_list, command_buffer);
  command_buffer->EndRenderPass();

  EndFrame(nullptr, nullptr);

  // NOTE: the bias matrix used for shadowmapping in Vulkan is different than
  // OpenGL, so we can't use glm::scaleBias().
  const mat4 bias(0.5, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                  0.5, 0.5, 0.0, 1.0);

  return fxl::AdoptRef(new ShadowMap(
      std::move(color_image), bias * camera.projection() * camera.transform(),
      light_color));
}

}  // namespace escher
