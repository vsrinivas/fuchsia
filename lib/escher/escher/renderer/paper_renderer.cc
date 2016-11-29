// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/paper_renderer.h"

#include "escher/geometry/tessellation.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/image_cache.h"
#include "escher/impl/mesh_manager.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_pipeline_cache.h"
#include "escher/impl/model_renderer.h"
#include "escher/impl/ssdo_sampler.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

namespace escher {

namespace {

class PaperFramebuffer : public Framebuffer {
 public:
  PaperFramebuffer(impl::EscherImpl* escher,
                   uint32_t width,
                   uint32_t height,
                   std::vector<ImagePtr> images,
                   vk::RenderPass render_pass,
                   FramebufferPtr main_color_fb,
                   FramebufferPtr aux_color_fb,
                   TexturePtr depth_texture,
                   TexturePtr main_color_texture,
                   TexturePtr aux_color_texture,
                   const PaperRenderer* renderer_in)
      : Framebuffer(escher, width, height, std::move(images), render_pass),
        renderer(renderer_in),
        main_color_fb_(main_color_fb),
        aux_color_fb_(aux_color_fb),
        depth_texture_(depth_texture),
        main_color_texture_(main_color_texture),
        aux_color_texture_(aux_color_texture) {}

  const FramebufferPtr& main_color_fb() const { return main_color_fb_; }
  const FramebufferPtr& aux_color_fb() const { return aux_color_fb_; }
  const TexturePtr& depth_texture() const { return depth_texture_; }
  const TexturePtr& main_color_texture() const { return main_color_texture_; }
  const TexturePtr& aux_color_texture() const { return aux_color_texture_; }

  // Used to verify that only the renderer that created the framebuffer can
  // use it.
  const PaperRenderer* renderer;

 private:
  // Framebuffer which first holds the sampled (unfiltered) SSDO illumination
  // data, and then finally the fully-filtered SSDO illumination data.
  FramebufferPtr main_color_fb_;
  // Framebuffer which holds the horizontally-filtered SSDO illumination data.
  // This serves as the input to the vertical filter, which writes into
  // main_color_fb_.
  FramebufferPtr aux_color_fb_;
  // Depth texture, used to sample the depth-buffer in order to generate the
  // sampled (unfiltered) SSDO illumination data.
  TexturePtr depth_texture_;
  // Color texture.
  TexturePtr main_color_texture_;
  TexturePtr aux_color_texture_;
};

}  // namespace

PaperRenderer::PaperRenderer(impl::EscherImpl* escher)
    : Renderer(escher),
      full_screen_(NewFullScreenMesh(escher->mesh_manager())),
      image_cache_(escher->image_cache()),
      // TODO: perhaps cache depth_format_ in EscherImpl.
      depth_format_(ESCHER_CHECKED_VK_RESULT(
          impl::GetSupportedDepthFormat(context_.physical_device))),
      // TODO: could potentially share ModelData/PipelineCache/ModelRenderer
      // between multiple PaperRenderers.
      model_data_(std::make_unique<impl::ModelData>(context_.device,
                                                    escher->gpu_allocator())),
      // TODO: VulkanProvider should know the swapchain format and we should use
      // it.  Or, the PaperRenderer could be passed the format as a parameter.
      model_renderer_(std::make_unique<impl::ModelRenderer>(
          escher,
          model_data_.get(),
          vk::Format::eB8G8R8A8Srgb,
          ESCHER_CHECKED_VK_RESULT(
              impl::GetSupportedDepthFormat(context_.physical_device)))),
      ssdo_(std::make_unique<impl::SsdoSampler>(
          context_.device,
          full_screen_,
          escher->image_cache()->NewNoiseImage(impl::SsdoSampler::kNoiseSize,
                                               impl::SsdoSampler::kNoiseSize),
          escher->glsl_compiler())),
      clear_values_({vk::ClearColorValue(
                         std::array<float, 4>{{0.012, 0.047, 0.427, 1.f}}),
                     vk::ClearDepthStencilValue(1.f, 0)}) {}

PaperRenderer::~PaperRenderer() {
  escher_->command_buffer_pool()->Cleanup();
  if (escher_->transfer_command_buffer_pool()) {
    escher_->transfer_command_buffer_pool()->Cleanup();
  }
}

FramebufferPtr PaperRenderer::NewFramebuffer(const ImagePtr& main_color_image) {
  uint32_t width = main_color_image->width();
  uint32_t height = main_color_image->height();

  ImagePtr depth_image = image_cache_->GetDepthImage(
      depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  ImagePtr aux_color_image = escher_->image_cache()->NewColorAttachmentImage(
      width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  return ftl::MakeRefCounted<PaperFramebuffer>(
      escher_, width, height,
      std::vector<ImagePtr>{main_color_image, depth_image},
      model_renderer_->depth_prepass(),
      ftl::MakeRefCounted<Framebuffer>(escher_, width, height,
                                       std::vector<ImagePtr>{main_color_image},
                                       ssdo_->render_pass()),
      ftl::MakeRefCounted<Framebuffer>(escher_, width, height,
                                       std::vector<ImagePtr>{aux_color_image},
                                       ssdo_->render_pass()),
      ftl::MakeRefCounted<Texture>(depth_image, context_.device,
                                   vk::Filter::eNearest,
                                   vk::ImageAspectFlagBits::eDepth),
      ftl::MakeRefCounted<Texture>(main_color_image, context_.device,
                                   vk::Filter::eNearest),
      ftl::MakeRefCounted<Texture>(aux_color_image, context_.device,
                                   vk::Filter::eNearest),
      this);
}

void PaperRenderer::DrawDepthPrePass(const FramebufferPtr& framebuffer,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer);

  model_renderer_->hack_use_depth_prepass = true;
  command_buffer->BeginRenderPass(model_renderer_->depth_prepass(), framebuffer,
                                  clear_values_);
  model_renderer_->Draw(stage, model, command_buffer, TexturePtr());
  command_buffer->EndRenderPass();
}

void PaperRenderer::DrawSsdoPasses(const FramebufferPtr& framebuffer_in,
                                   Stage& stage) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer_in);

  auto framebuffer = static_cast<PaperFramebuffer*>(framebuffer_in.get());
  FTL_DCHECK(framebuffer->renderer == this);

  // Prepare to sample from the depth buffer.
  auto& depth_texture = framebuffer->depth_texture();
  command_buffer->AddUsedResource(depth_texture);

  command_buffer->TransitionImageLayout(
      depth_texture->image(), vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  impl::SsdoSampler::SamplerConfig sampler_config(stage);
  ssdo_->Sample(command_buffer, framebuffer->aux_color_fb(), depth_texture,
                &sampler_config, clear_values_);

  // Now that we have finished sampling the depth buffer, transition it for
  // reuse as a depth buffer in the lighting pass.
  command_buffer->TransitionImageLayout(
      depth_texture->image(), vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

  // Do two filter passes, one horizontal and one vertical.
  constexpr bool kSkipFiltering = false;
  if (!kSkipFiltering) {
    {
      auto& aux_color_texture = framebuffer->aux_color_texture();
      command_buffer->AddUsedResource(aux_color_texture);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(1.f / stage.viewing_volume().width(), 0.f);
      filter_config.scene_depth = stage.viewing_volume().depth_range();
      ssdo_->Filter(command_buffer, framebuffer->main_color_fb(),
                    aux_color_texture, &filter_config, clear_values_);

      command_buffer->TransitionImageLayout(
          aux_color_texture->image(), vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);
    }
    {
      auto& main_color_texture = framebuffer->main_color_texture();
      command_buffer->AddUsedResource(main_color_texture);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(0.f, 1.f / stage.viewing_volume().height());
      filter_config.scene_depth = stage.viewing_volume().depth_range();
      ssdo_->Filter(command_buffer, framebuffer->aux_color_fb(),
                    main_color_texture, &filter_config, clear_values_);

      command_buffer->TransitionImageLayout(
          main_color_texture->image(), vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);
    }
  }
}

void PaperRenderer::DrawLightingPass(const FramebufferPtr& framebuffer_in,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer_in);

  auto framebuffer = static_cast<PaperFramebuffer*>(framebuffer_in.get());
  FTL_DCHECK(framebuffer->renderer == this);

  // When we disable lighting, we don't do the SSDO sample/filter passes.
  // Since we no longer have the resulting texture containing illumination data,
  // we use a single-pixel white texture to light everything 100%.
  auto& illumination_texture = enable_lighting_
                                   ? framebuffer->aux_color_texture()
                                   : model_renderer_->white_texture();

  model_renderer_->hack_use_depth_prepass = false;

  // Update the clear color from the stage
  vec3 clear_color = stage.clear_color();
  clear_values_[0] = vk::ClearColorValue(
      std::array<float, 4>{{clear_color.x, clear_color.y, clear_color.z, 1.f}});

  // Render
  command_buffer->BeginRenderPass(model_renderer_->lighting_pass(),
                                  framebuffer_in, clear_values_);
  model_renderer_->Draw(stage, model, command_buffer, illumination_texture);
  command_buffer->EndRenderPass();

  if (show_debug_info_) {
    int32_t width = framebuffer->width();
    int32_t height = framebuffer->height();

    vk::ImageBlit blit;
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.srcOffsets[1] = vk::Offset3D{width, height, 1};
    blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = vk::Offset3D{width * 3 / 4, 0, 0};
    blit.dstOffsets[1] = vk::Offset3D{width, height / 4, 1};
    command_buffer->get().blitImage(
        framebuffer->aux_color_texture()->image()->get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        framebuffer->main_color_texture()->image()->get(),
        vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eLinear);
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
    blit.dstOffsets[0] = vk::Offset3D{width * 3 / 4, height / 4, 0};
    blit.dstOffsets[1] = vk::Offset3D{width, height / 2, 1};
    command_buffer->get().blitImage(
        framebuffer->depth_texture()->image()->get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        framebuffer->main_color_texture()->image()->get(),
        vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eLinear);
  }

  // ModelRenderer's lighting render-pass leaves the color-attachment format
  // as eColorAttachmentOptimal, since it's not clear how it will be used
  // next.
  // We could push this flexibility farther by letting our client specify
  // the
  // desired output format, but for now we'll assume that the image is being
  // presented immediately.
  command_buffer->TransitionImageLayout(
      framebuffer->main_color_texture()->image(),
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR);
}

void PaperRenderer::DrawFrame(Stage& stage,
                              Model& model,
                              const FramebufferPtr& framebuffer,
                              const SemaphorePtr& frame_done,
                              FrameRetiredCallback frame_retired_callback) {
  BeginFrame(framebuffer);
  DrawDepthPrePass(framebuffer, stage, model);
  SubmitPartialFrame();

  if (enable_lighting_) {
    DrawSsdoPasses(framebuffer, stage);
    SubmitPartialFrame();
  }

  DrawLightingPass(framebuffer, stage, model);
  EndFrame(frame_done, frame_retired_callback);
}

}  // namespace escher
