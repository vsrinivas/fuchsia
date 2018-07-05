// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/paper_renderer.h"

#include "lib/escher/geometry/tessellation.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/impl/model_data.h"
#include "lib/escher/impl/model_depth_pass.h"
#include "lib/escher/impl/model_display_list.h"
#include "lib/escher/impl/model_lighting_pass.h"
#include "lib/escher/impl/model_moment_shadow_map_lighting_pass.h"
#include "lib/escher/impl/model_pipeline_cache.h"
#include "lib/escher/impl/model_render_pass.h"
#include "lib/escher/impl/model_renderer.h"
#include "lib/escher/impl/model_shadow_map_lighting_pass.h"
#include "lib/escher/impl/ssdo_accelerator.h"
#include "lib/escher/impl/ssdo_sampler.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/renderer/moment_shadow_map.h"
#include "lib/escher/renderer/shadow_map.h"
#include "lib/escher/scene/camera.h"
#include "lib/escher/scene/model.h"
#include "lib/escher/util/depth_to_color.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/util/trace_macros.h"
#include "lib/escher/vk/framebuffer.h"
#include "lib/escher/vk/image.h"

namespace escher {

using impl::ModelDisplayListFlag;
using impl::ModelDisplayListPtr;

namespace {
constexpr float kMaxDepth = 1.f;

// Amount by which the SsdoAccelerator table is scaled down in each dimension,
// not including bit-packing.
constexpr uint32_t kSsdoAccelDownsampleFactor =
    impl::SsdoSampler::kSsdoAccelDownsampleFactor;

constexpr bool kSkipFiltering = false;

constexpr uint32_t kLightingPassSampleCount = 1;

}  // namespace

fxl::RefPtr<PaperRenderer> PaperRenderer::New(EscherWeakPtr escher) {
  return fxl::MakeRefCounted<PaperRenderer>(
      escher, fxl::MakeRefCounted<impl::ModelData>(escher));
}

PaperRenderer::PaperRenderer(EscherWeakPtr weak_escher,
                             impl::ModelDataPtr model_data)
    : Renderer(std::move(weak_escher)),
      full_screen_(NewFullScreenMesh(escher()->mesh_manager())),
      image_cache_(escher()->image_cache()),
      depth_format_(ESCHER_CHECKED_VK_RESULT(
          impl::GetSupportedDepthStencilFormat(context_.physical_device))),
      model_data_(std::move(model_data)),
      model_renderer_(
          impl::ModelRenderer::New(GetEscherWeakPtr(), model_data_)),
      ssdo_(std::make_unique<impl::SsdoSampler>(
          GetEscherWeakPtr(), full_screen_,
          image_utils::NewNoiseImage(image_cache_, escher()->gpu_uploader(),
                                     impl::SsdoSampler::kNoiseSize,
                                     impl::SsdoSampler::kNoiseSize,
                                     vk::ImageUsageFlagBits::eSampled),
          model_data_.get())),
      ssdo_accelerator_(std::make_unique<impl::SsdoAccelerator>(
          GetEscherWeakPtr(), image_cache_)),
      depth_to_color_(
          std::make_unique<DepthToColor>(GetEscherWeakPtr(), image_cache_)),
      clear_values_(
          {vk::ClearColorValue(std::array<float, 4>{{0.f, 0.f, 0.f, 1.f}}),
           vk::ClearDepthStencilValue(kMaxDepth, 0)}),
      ambient_light_color_(1.f) {}

PaperRenderer::~PaperRenderer() { escher()->Cleanup(); }

void PaperRenderer::DrawDepthPrePass(const FramePtr& frame,
                                     const ImagePtr& depth_image,
                                     const ImagePtr& dummy_color_image,
                                     float scale, const Stage& stage,
                                     const Model& model, const Camera& camera) {
  TRACE_DURATION("gfx", "escher::PaperRenderer::DrawDepthPrePass", "width",
                 depth_image->width(), "height", depth_image->height());

  auto command_buffer = frame->command_buffer();

  FramebufferPtr framebuffer = fxl::MakeRefCounted<Framebuffer>(
      escher(), depth_image->width(), depth_image->height(),
      std::vector<ImagePtr>{dummy_color_image, depth_image},
      // TODO: pass escher::RenderPass instead of vk::RenderPass?
      depth_pass_->vk());

  auto display_list_flags =
      (sort_by_pipeline_ ? ModelDisplayListFlag::kSortByPipeline
                         : ModelDisplayListFlag::kNull);

  ModelDisplayListPtr display_list = model_renderer_->CreateDisplayList(
      stage, model, camera, depth_pass_, display_list_flags, scale,
      TexturePtr(), mat4(1.f), vec3(1.f), vec3(1.f), command_buffer);

  command_buffer->KeepAlive(framebuffer);
  command_buffer->KeepAlive(display_list);
  command_buffer->BeginRenderPass(
      depth_pass_, framebuffer, clear_values_,
      camera.viewport().vk_rect_2d(framebuffer->width(),
                                   framebuffer->height()));
  model_renderer_->Draw(stage, display_list, command_buffer, camera.viewport());
  command_buffer->EndRenderPass();
}

void PaperRenderer::DrawSsdoPasses(const FramePtr& frame,
                                   const ImagePtr& depth_in,
                                   const ImagePtr& color_out,
                                   const ImagePtr& color_aux,
                                   const TexturePtr& accelerator_texture,
                                   const Stage& stage) {
  TRACE_DURATION("gfx", "escher::PaperRenderer::DrawSsdoPasses");

  FXL_DCHECK(color_out->width() == color_aux->width() &&
             color_out->height() == color_aux->height());
  uint32_t width = color_out->width();
  uint32_t height = color_out->height();

  auto command_buffer = frame->command_buffer();

  auto fb_out = fxl::MakeRefCounted<Framebuffer>(
      escher(), width, height, std::vector<ImagePtr>{color_out},
      ssdo_->render_pass());

  auto fb_aux = fxl::MakeRefCounted<Framebuffer>(
      escher(), width, height, std::vector<ImagePtr>{color_aux},
      ssdo_->render_pass());

  command_buffer->KeepAlive(fb_out);
  command_buffer->KeepAlive(fb_aux);
  command_buffer->KeepAlive(accelerator_texture);

  TexturePtr depth_texture = fxl::MakeRefCounted<Texture>(
      escher()->resource_recycler(), depth_in, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eDepth);
  command_buffer->KeepAlive(depth_texture);

  // Prepare to sample from the depth buffer.
  command_buffer->TransitionImageLayout(
      depth_in, vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  frame->AddTimestamp("finished layout transition before SSDO sampling");

  impl::SsdoSampler::SamplerConfig sampler_config(stage);
  ssdo_->Sample(command_buffer, fb_out, depth_texture, accelerator_texture,
                &sampler_config);

  frame->AddTimestamp("finished SSDO sampling");

  // Now that we have finished sampling the depth buffer, transition it for
  // reuse as a depth buffer in the lighting pass.
  command_buffer->TransitionImageLayout(
      depth_in, vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

  frame->AddTimestamp("finished layout transition before SSDO filtering");

  // Do two filter passes, one horizontal and one vertical.
  if (!kSkipFiltering) {
    {
      auto color_out_tex = fxl::MakeRefCounted<Texture>(
          escher()->resource_recycler(), color_out, vk::Filter::eNearest);
      command_buffer->KeepAlive(color_out_tex);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(1.f / stage.viewing_volume().width(), 0.f);
      filter_config.scene_depth = stage.viewing_volume().depth();
      ssdo_->Filter(command_buffer, fb_aux, color_out_tex, accelerator_texture,
                    &filter_config);

      command_buffer->TransitionImageLayout(
          color_out, vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);

      frame->AddTimestamp("finished SSDO filter pass 1");
    }
    {
      auto color_aux_tex = fxl::MakeRefCounted<Texture>(
          escher()->resource_recycler(), color_aux, vk::Filter::eNearest);
      command_buffer->KeepAlive(color_aux_tex);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(0.f, 1.f / stage.viewing_volume().height());
      filter_config.scene_depth = stage.viewing_volume().depth();
      ssdo_->Filter(command_buffer, fb_out, color_aux_tex, accelerator_texture,
                    &filter_config);

      command_buffer->TransitionImageLayout(
          color_aux, vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);

      frame->AddTimestamp("finished SSDO filter pass 2");
    }
  }
}

void PaperRenderer::UpdateRenderPasses(vk::Format prepass_color_format,
                                       vk::Format lighting_pass_color_format) {
  if (!depth_pass_ || depth_pass_->color_format() != prepass_color_format) {
    FXL_VLOG(1) << "PaperRenderer: updating ModelDepthPass.";
    depth_pass_ = fxl::MakeRefCounted<impl::ModelDepthPass>(
        escher()->resource_recycler(), model_data_, prepass_color_format,
        depth_format_, 1);
  }

  if (!ssdo_lighting_pass_ ||
      ssdo_lighting_pass_->color_format() != lighting_pass_color_format) {
    FXL_VLOG(1) << "PaperRenderer: updating ModelLightingPass.";
    ssdo_lighting_pass_ = fxl::MakeRefCounted<impl::ModelLightingPass>(
        escher()->resource_recycler(), model_data_, prepass_color_format,
        depth_format_, kLightingPassSampleCount);

    // TODO: use separate pass for no-shadows lighting: don't bother reading
    // from the lighting texture.
    no_shadow_lighting_pass_ = ssdo_lighting_pass_;
  }

  if (!shadow_map_lighting_pass_ ||
      shadow_map_lighting_pass_->color_format() != lighting_pass_color_format) {
    FXL_VLOG(1) << "PaperRenderer: updating ModelLightingPass.";
    shadow_map_lighting_pass_ =
        fxl::MakeRefCounted<impl::ModelShadowMapLightingPass>(
            escher()->resource_recycler(), model_data_, prepass_color_format,
            depth_format_, kLightingPassSampleCount);
  }

  if (!moment_shadow_map_lighting_pass_ ||
      moment_shadow_map_lighting_pass_->color_format() !=
          lighting_pass_color_format) {
    FXL_VLOG(1) << "PaperRenderer: updating ModelLightingPass.";
    moment_shadow_map_lighting_pass_ =
        fxl::MakeRefCounted<impl::ModelMomentShadowMapLightingPass>(
            escher()->resource_recycler(), model_data_, prepass_color_format,
            depth_format_, kLightingPassSampleCount);
  }
}

void PaperRenderer::DrawLightingPass(
    const FramePtr& frame, uint32_t sample_count,
    const FramebufferPtr& framebuffer, const TexturePtr& shadow_texture,
    const mat4& shadow_matrix, const vec3& ambient_light_color,
    const vec3& direct_light_color, const impl::ModelRenderPassPtr& render_pass,
    const Stage& stage, const Model& model, const Camera& camera,
    const Model* overlay_model) {
  TRACE_DURATION("gfx", "escher::PaperRenderer::DrawLightingPass", "width",
                 framebuffer->width(), "height", framebuffer->height());

  auto command_buffer = frame->command_buffer();
  command_buffer->KeepAlive(framebuffer);

  auto display_list_flags =
      (sort_by_pipeline_ ? ModelDisplayListFlag::kSortByPipeline
                         : ModelDisplayListFlag::kNull);

  ModelDisplayListPtr display_list = model_renderer_->CreateDisplayList(
      stage, model, camera, render_pass, display_list_flags, 1.f,
      shadow_texture, shadow_matrix, ambient_light_color, direct_light_color,
      command_buffer);
  command_buffer->KeepAlive(display_list);

  // Update the clear color from the stage
  vec4 clear_color = stage.clear_color();
  clear_values_[0] = vk::ClearColorValue(std::array<float, 4>{
      {clear_color.x, clear_color.y, clear_color.z, clear_color.w}});

  // Create stage, camera etc. for rendering overlays.
  Stage overlay_stage;
  overlay_stage.set_viewing_volume(stage.viewing_volume());
  Camera overlay_camera = Camera::NewOrtho(overlay_stage.viewing_volume());
  impl::ModelDisplayListPtr overlay_display_list;
  if (overlay_model && !overlay_model->objects().empty()) {
    display_list_flags = ModelDisplayListFlag::kDisableDepthTest;

    overlay_display_list = model_renderer_->CreateDisplayList(
        overlay_stage, *overlay_model, overlay_camera, no_shadow_lighting_pass_,
        display_list_flags, 1.f, TexturePtr(), mat4(1.f), ambient_light_color,
        direct_light_color, command_buffer);
    command_buffer->KeepAlive(overlay_display_list);
  }

  command_buffer->BeginRenderPass(
      render_pass, framebuffer, clear_values_,
      camera.viewport().vk_rect_2d(framebuffer->width(),
                                   framebuffer->height()));

  model_renderer_->Draw(stage, display_list, command_buffer, camera.viewport());
  if (overlay_display_list) {
    model_renderer_->Draw(stage, overlay_display_list, command_buffer,
                          camera.viewport());
  }

  command_buffer->EndRenderPass();
}

void PaperRenderer::DrawDebugOverlays(const FramePtr& frame,
                                      const ImagePtr& output,
                                      const ImagePtr& depth,
                                      const ImagePtr& illumination,
                                      const TexturePtr& ssdo_accel,
                                      const TexturePtr& ssdo_accel_depth) {
  if (show_debug_info_) {
    TexturePtr ssdo_accel_depth_as_color =
        depth_to_color_->Convert(frame, ssdo_accel_depth,
                                 vk::ImageUsageFlagBits::eStorage |
                                     vk::ImageUsageFlagBits::eTransferSrc);

    int32_t dst_width = output->width();
    int32_t dst_height = output->height();
    int32_t src_width = 0;
    int32_t src_height = 0;

    vk::ImageBlit blit;
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    // Used to visualize both the SSDO acceleration look-up table, as well as
    // the depth image that was used to generate it.
    src_width = depth->width() / kSsdoAccelDownsampleFactor;
    src_height = depth->height() / kSsdoAccelDownsampleFactor;
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcOffsets[1] = vk::Offset3D{src_width, src_height, 1};

    // Show the depth texture used as input to the SSDO accelerator.
    blit.dstOffsets[0] = vk::Offset3D{dst_width * 3 / 4, 0, 0};
    blit.dstOffsets[1] = vk::Offset3D{dst_width, dst_height / 4, 1};
    frame->vk_command_buffer().blitImage(
        ssdo_accel_depth_as_color->vk_image(),
        vk::ImageLayout::eShaderReadOnlyOptimal, output->vk(),
        vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eNearest);

    // Show the lookup table generated by the SSDO accelerator.
    TexturePtr unpacked_ssdo_accel = ssdo_accelerator_->UnpackLookupTable(
        frame, ssdo_accel, src_width, src_height);
    FXL_DCHECK(unpacked_ssdo_accel->width() ==
               static_cast<uint32_t>(src_width));
    FXL_DCHECK(unpacked_ssdo_accel->height() ==
               static_cast<uint32_t>(src_height));
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcOffsets[1] = vk::Offset3D{src_width, src_height, 1};
    blit.dstOffsets[0] = vk::Offset3D{dst_width * 3 / 4, dst_height * 1 / 4, 0};
    blit.dstOffsets[1] = vk::Offset3D{dst_width, dst_height * 1 / 2, 1};
    frame->vk_command_buffer().blitImage(
        unpacked_ssdo_accel->vk_image(), vk::ImageLayout::eGeneral,
        output->vk(), vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eNearest);

    // Show the illumination texture.
    if (illumination) {
      src_width = illumination->width();
      src_height = illumination->height();
      blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      blit.srcOffsets[1] = vk::Offset3D{src_width, src_height, 1};
      blit.dstOffsets[0] =
          vk::Offset3D{dst_width * 3 / 4, dst_height * 1 / 2, 0};
      blit.dstOffsets[1] = vk::Offset3D{dst_width, dst_height * 3 / 4, 1};
      frame->vk_command_buffer().blitImage(
          illumination->vk(), vk::ImageLayout::eShaderReadOnlyOptimal,
          output->vk(), vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
          vk::Filter::eLinear);
    }

    frame->AddTimestamp("finished blitting debug overlay");
  }
}

void PaperRenderer::DrawFrame(const FramePtr& frame, const Stage& stage,
                              const Model& model, const Camera& camera,
                              const ImagePtr& color_image_out,
                              const ShadowMapPtr& shadow_map,
                              const Model* overlay_model) {
  TRACE_DURATION("gfx", "escher::PaperRenderer::DrawFrame");

  UpdateRenderPasses(color_image_out->format(), color_image_out->format());

  switch (shadow_type_) {
    case PaperRendererShadowType::kNone:
      DrawFrameWithNoShadows(frame, stage, model, camera, color_image_out,
                             overlay_model);
      break;
    case PaperRendererShadowType::kSsdo:
      DrawFrameWithSsdoShadows(frame, stage, model, camera, color_image_out,
                               overlay_model);
      break;
    case PaperRendererShadowType::kShadowMap:
      DrawFrameWithShadowMapShadows(frame, stage, model, camera,
                                    color_image_out, shadow_map, overlay_model);
      break;
    case PaperRendererShadowType::kMomentShadowMap:
      DrawFrameWithMomentShadowMapShadows(frame, stage, model, camera,
                                          color_image_out, shadow_map,
                                          overlay_model);
      break;
  }

  // ModelRenderer's lighting render-pass leaves the color-attachment format
  // as eColorAttachmentOptimal, since it's not clear how it will be used
  // next.
  // We could push this flexibility farther by letting our client specify the
  // desired output format, but for now we'll assume that the image is being
  // presented immediately.
  frame->command_buffer()->TransitionImageLayout(
      color_image_out, vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR);

  frame->AddTimestamp("finished transition to presentation layout");
}

void PaperRenderer::DrawFrameWithNoShadows(const FramePtr& frame,
                                           const Stage& stage,
                                           const Model& model,
                                           const Camera& camera,
                                           const ImagePtr& color_image_out,
                                           const Model* overlay_model) {
  uint32_t width = color_image_out->width();
  uint32_t height = color_image_out->height();

  frame->command_buffer()->TakeWaitSemaphore(
      color_image_out, vk::PipelineStageFlagBits::eColorAttachmentOutput);

  frame->command_buffer()->TransitionImageLayout(
      color_image_out, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);

  ImagePtr depth_image = image_utils::NewDepthImage(
      image_cache_, depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  FramebufferPtr framebuffer = fxl::MakeRefCounted<Framebuffer>(
      escher(), width, height,
      std::vector<ImagePtr>{color_image_out, depth_image},
      // TODO: pass escher::RenderPass instead of vk::RenderPass?
      no_shadow_lighting_pass_->vk());

  const vec3 kAmbientLightColor(1.f);
  const vec3 kDirectionalLightColor(0.f);
  DrawLightingPass(frame, kLightingPassSampleCount, framebuffer, TexturePtr(),
                   mat4(1.f), kAmbientLightColor, kDirectionalLightColor,
                   no_shadow_lighting_pass_, stage, model, camera,
                   overlay_model);
}

void PaperRenderer::DrawFrameWithShadowMapShadows(
    const FramePtr& frame, const Stage& stage, const Model& model,
    const Camera& camera, const ImagePtr& color_image_out,
    const ShadowMapPtr& shadow_map, const Model* overlay_model) {
  FXL_DCHECK(shadow_map);
  FXL_DCHECK(shadow_map->IsKindOf<ShadowMap>());

  uint32_t width = color_image_out->width();
  uint32_t height = color_image_out->height();

  frame->command_buffer()->TakeWaitSemaphore(
      color_image_out, vk::PipelineStageFlagBits::eColorAttachmentOutput);

  frame->command_buffer()->TransitionImageLayout(
      color_image_out, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);

  ImagePtr depth_image = image_utils::NewDepthImage(
      image_cache_, depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  FramebufferPtr framebuffer = fxl::MakeRefCounted<Framebuffer>(
      escher(), width, height,
      std::vector<ImagePtr>{color_image_out, depth_image},
      // TODO: pass escher::RenderPass instead of vk::RenderPass?
      shadow_map_lighting_pass_->vk());

  DrawLightingPass(frame, kLightingPassSampleCount, framebuffer,
                   shadow_map->texture(), shadow_map->matrix(),
                   stage.fill_light().color(), shadow_map->light_color(),
                   shadow_map_lighting_pass_, stage, model, camera,
                   overlay_model);

  frame->AddTimestamp("finished shadow map lighting pass");
}

void PaperRenderer::DrawFrameWithMomentShadowMapShadows(
    const FramePtr& frame, const Stage& stage, const Model& model,
    const Camera& camera, const ImagePtr& color_image_out,
    const ShadowMapPtr& shadow_map, const Model* overlay_model) {
  FXL_DCHECK(shadow_map);
  FXL_DCHECK(shadow_map->IsKindOf<MomentShadowMap>());

  uint32_t width = color_image_out->width();
  uint32_t height = color_image_out->height();

  frame->command_buffer()->TakeWaitSemaphore(
      color_image_out, vk::PipelineStageFlagBits::eColorAttachmentOutput);

  frame->command_buffer()->TransitionImageLayout(
      color_image_out, vk::ImageLayout::eUndefined,
      vk::ImageLayout::eColorAttachmentOptimal);

  ImagePtr depth_image = image_utils::NewDepthImage(
      image_cache_, depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  FramebufferPtr framebuffer = fxl::MakeRefCounted<Framebuffer>(
      escher(), width, height,
      std::vector<ImagePtr>{color_image_out, depth_image},
      moment_shadow_map_lighting_pass_->vk());

  DrawLightingPass(frame, kLightingPassSampleCount, framebuffer,
                   shadow_map->texture(), shadow_map->matrix(),
                   stage.fill_light().color(), shadow_map->light_color(),
                   moment_shadow_map_lighting_pass_, stage, model, camera,
                   overlay_model);

  frame->AddTimestamp("finished moment shadow map lighting pass");
}

void PaperRenderer::DrawFrameWithSsdoShadows(const FramePtr& frame,
                                             const Stage& stage,
                                             const Model& model,
                                             const Camera& camera,
                                             const ImagePtr& color_image_out,
                                             const Model* overlay_model) {
  uint32_t width = color_image_out->width();
  uint32_t height = color_image_out->height();
  uint32_t ssdo_accel_width = width / kSsdoAccelDownsampleFactor;
  uint32_t ssdo_accel_height = height / kSsdoAccelDownsampleFactor;

  // Downsized depth-only prepass for SSDO acceleration.
  if (width % kSsdoAccelDownsampleFactor != 0u) {
    // Round up to the nearest multiple.
    ssdo_accel_width += 1;
  }

  if (height % kSsdoAccelDownsampleFactor != 0u) {
    // Round up to the nearest multiple.
    ssdo_accel_height += 1;
  }

  ImagePtr ssdo_accel_depth_image = image_utils::NewDepthImage(
      image_cache_, depth_format_, ssdo_accel_width, ssdo_accel_height,
      vk::ImageUsageFlagBits::eSampled);
  TexturePtr ssdo_accel_depth_texture = fxl::MakeRefCounted<Texture>(
      escher()->resource_recycler(), ssdo_accel_depth_image,
      vk::Filter::eNearest, vk::ImageAspectFlagBits::eDepth,
      // TODO: use a more descriptive enum than true.
      true);
  {
    // TODO: maybe share this with SsdoAccelerator::GenerateLookupTable().
    // However, this would require refactoring to match the color format
    // expected by ModelRenderer.
    ImagePtr ssdo_accel_dummy_color_image = image_cache_->NewImage(
        {color_image_out->format(), ssdo_accel_width, ssdo_accel_height, 1,
         vk::ImageUsageFlagBits::eColorAttachment});

    // TODO(ES-41): The 1/kSsdoAccelDownsampleFactor is not 100% correct in
    // the
    // case where we needed to round up the acceleration image width and/or
    // height.  To correct this, we would change the single scale factor to a
    // scale_x and scale_y, and adjust each as necessary (i.e. maybe slightly
    // larger, so that the stage completely fills the depth image).
    DrawDepthPrePass(frame, ssdo_accel_depth_image,
                     ssdo_accel_dummy_color_image,
                     1.f / kSsdoAccelDownsampleFactor, stage, model, camera);
    frame->SubmitPartialFrame(nullptr);
    frame->AddTimestamp("finished SSDO acceleration depth pre-pass");
  }

  // Compute SSDO acceleration structure.
  TexturePtr ssdo_accel_texture = ssdo_accelerator_->GenerateLookupTable(
      frame, ssdo_accel_depth_texture,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
  frame->SubmitPartialFrame(nullptr);

  // Depth-only pre-pass.
  ImagePtr depth_image = image_utils::NewDepthImage(
      image_cache_, depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
  {
    // TODO(ES-43): this can be deferred until the final lighting pass.
    frame->command_buffer()->TakeWaitSemaphore(
        color_image_out, vk::PipelineStageFlagBits::eColorAttachmentOutput);

    DrawDepthPrePass(frame, depth_image, color_image_out, 1.f, stage, model,
                     camera);
    frame->SubmitPartialFrame(nullptr);
    frame->AddTimestamp("finished depth pre-pass");
  }

  // Compute the illumination and store the result in a texture.
  TexturePtr shadow_texture;
  ImagePtr illum1 =
      image_cache_->NewImage({ssdo_->color_format(), width, height, 1,
                              vk::ImageUsageFlagBits::eSampled |
                                  vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eStorage |
                                  vk::ImageUsageFlagBits::eTransferSrc});

  ImagePtr illum2 =
      image_cache_->NewImage({ssdo_->color_format(), width, height, 1,
                              vk::ImageUsageFlagBits::eSampled |
                                  vk::ImageUsageFlagBits::eColorAttachment |
                                  vk::ImageUsageFlagBits::eStorage |
                                  vk::ImageUsageFlagBits::eTransferSrc});

  DrawSsdoPasses(frame, depth_image, illum1, illum2, ssdo_accel_texture, stage);
  frame->SubmitPartialFrame(nullptr);

  shadow_texture = fxl::MakeRefCounted<Texture>(escher()->resource_recycler(),
                                                illum1, vk::Filter::eNearest);

  // Done after previous SubmitPartialFrame(), because this is needed by the
  // final lighting pass.
  frame->command_buffer()->KeepAlive(shadow_texture);

  // Use multisampling for final lighting pass, or not.
  if (kLightingPassSampleCount == 1) {
    FramebufferPtr lighting_fb = fxl::MakeRefCounted<Framebuffer>(
        escher(), width, height,
        std::vector<ImagePtr>{color_image_out, depth_image},
        // TODO: pass escher::RenderPass instead of vk::RenderPass?
        ssdo_lighting_pass_->vk());

    frame->command_buffer()->KeepAlive(lighting_fb);

    DrawLightingPass(frame, kLightingPassSampleCount, lighting_fb,
                     shadow_texture, mat4(1.f), stage.fill_light().color(),
                     stage.key_light().color(), ssdo_lighting_pass_, stage,
                     model, camera, overlay_model);

    frame->AddTimestamp("finished lighting pass");
  } else {
    ImageInfo info;
    info.width = width;
    info.height = height;
    info.sample_count = kLightingPassSampleCount;
    info.format = color_image_out->format();
    info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                 vk::ImageUsageFlagBits::eTransferSrc;
    ImagePtr color_image_multisampled = image_cache_->NewImage(info);

    // TODO: use lazily-allocated image: since we don't care about saving the
    // depth buffer, a tile-based GPU doesn't actually need this memory.
    info.format = depth_format_;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    ImagePtr depth_image_multisampled = image_cache_->NewImage(info);

    FramebufferPtr multisample_fb = fxl::MakeRefCounted<Framebuffer>(
        escher(), width, height,
        std::vector<ImagePtr>{color_image_multisampled,
                              depth_image_multisampled},
        // TODO: pass escher::RenderPass instead of vk::RenderPass?
        ssdo_lighting_pass_->vk());

    frame->command_buffer()->KeepAlive(multisample_fb);

    DrawLightingPass(frame, kLightingPassSampleCount, multisample_fb,
                     shadow_texture, mat4(1.f), stage.fill_light().color(),
                     stage.key_light().color(), ssdo_lighting_pass_, stage,
                     model, camera, overlay_model);

    frame->AddTimestamp("finished lighting pass");

    // TODO: do this during lighting sub-pass by adding a resolve attachment.
    vk::ImageResolve resolve;
    vk::ImageSubresourceLayers layers;
    layers.aspectMask = vk::ImageAspectFlagBits::eColor;
    layers.mipLevel = 0;
    layers.baseArrayLayer = 0;
    layers.layerCount = 1;
    resolve.srcSubresource = layers;
    resolve.srcOffset = vk::Offset3D{0, 0, 0};
    resolve.dstSubresource = layers;
    resolve.dstOffset = vk::Offset3D{0, 0, 0};
    resolve.extent = vk::Extent3D{width, height, 0};
    frame->vk_command_buffer().resolveImage(
        color_image_multisampled->vk(),
        vk::ImageLayout::eColorAttachmentOptimal, color_image_out->vk(),
        vk::ImageLayout::eColorAttachmentOptimal, resolve);

    frame->AddTimestamp("finished multisample resolve");
  }

  DrawDebugOverlays(frame, color_image_out, depth_image,
                    shadow_texture ? shadow_texture->image() : ImagePtr(),
                    ssdo_accel_texture, ssdo_accel_depth_texture);
}

void PaperRenderer::set_enable_ssdo_acceleration(bool b) {
  ssdo_accelerator_->set_enabled(b);
}

}  // namespace escher
