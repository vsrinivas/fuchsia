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

void PaperRenderer::DrawSsdoPasses(const TexturePtr& depth_in,
                                   const ImagePtr& color_out,
                                   const ImagePtr& color_aux,
                                   Stage& stage) {
  FTL_DCHECK(color_out->width() == color_aux->width() &&
             color_out->height() == color_aux->height());
  uint32_t width = color_out->width();
  uint32_t height = color_out->height();

  auto command_buffer = current_frame();

  // Prepare to sample from the depth buffer.
  command_buffer->AddUsedResource(depth_in);
  command_buffer->TransitionImageLayout(
      depth_in->image(), vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  auto fb_out = ftl::MakeRefCounted<Framebuffer>(
      escher_, width, height, std::vector<ImagePtr>{color_out},
      ssdo_->render_pass());

  auto fb_aux = ftl::MakeRefCounted<Framebuffer>(
      escher_, width, height, std::vector<ImagePtr>{color_aux},
      ssdo_->render_pass());

  command_buffer->AddUsedResource(fb_out);
  command_buffer->AddUsedResource(fb_aux);

  impl::SsdoSampler::SamplerConfig sampler_config(stage);
  ssdo_->Sample(command_buffer, fb_out, depth_in, &sampler_config);

  // Now that we have finished sampling the depth buffer, transition it for
  // reuse as a depth buffer in the lighting pass.
  command_buffer->TransitionImageLayout(
      depth_in->image(), vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);

  // Do two filter passes, one horizontal and one vertical.
  constexpr bool kSkipFiltering = false;
  if (!kSkipFiltering) {
    {
      auto color_out_tex = ftl::MakeRefCounted<Texture>(
          color_out, context_.device, vk::Filter::eNearest);
      command_buffer->AddUsedResource(color_out_tex);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(1.f / stage.viewing_volume().width(), 0.f);
      filter_config.scene_depth = stage.viewing_volume().depth_range();
      ssdo_->Filter(command_buffer, fb_aux, color_out_tex, &filter_config);

      command_buffer->TransitionImageLayout(
          color_out, vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);
    }
    {
      auto color_aux_tex = ftl::MakeRefCounted<Texture>(
          color_aux, context_.device, vk::Filter::eNearest);
      command_buffer->AddUsedResource(color_aux_tex);

      impl::SsdoSampler::FilterConfig filter_config;
      filter_config.stride = vec2(0.f, 1.f / stage.viewing_volume().height());
      filter_config.scene_depth = stage.viewing_volume().depth_range();
      ssdo_->Filter(command_buffer, fb_out, color_aux_tex, &filter_config);

      command_buffer->TransitionImageLayout(
          color_aux, vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageLayout::eColorAttachmentOptimal);
    }
  }
}

void PaperRenderer::UpdateModelRenderer(vk::Format pre_pass_color_format,
                                        vk::Format lighting_pass_color_format) {
  // TODO: eventually, we should be able to handle it if the client changes the
  // format of the buffers that we are to render into.  For now, just lazily
  // create the ModelRenderer, and assume that it doesn't change.
  if (!model_renderer_) {
    model_renderer_ = std::make_unique<impl::ModelRenderer>(
        escher_, model_data_.get(), pre_pass_color_format,
        lighting_pass_color_format,
        ESCHER_CHECKED_VK_RESULT(
            impl::GetSupportedDepthFormat(context_.physical_device)));
  }
}

void PaperRenderer::DrawLightingPass(const FramebufferPtr& framebuffer,
                                     const TexturePtr& illumination_texture,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();

  model_renderer_->hack_use_depth_prepass = false;

  // Update the clear color from the stage
  vec3 clear_color = stage.clear_color();
  clear_values_[0] = vk::ClearColorValue(
      std::array<float, 4>{{clear_color.x, clear_color.y, clear_color.z, 1.f}});

  command_buffer->BeginRenderPass(model_renderer_->lighting_pass(), framebuffer,
                                  clear_values_.data(), clear_values_.size());
  model_renderer_->Draw(stage, model, command_buffer, illumination_texture);
  command_buffer->EndRenderPass();
}

void PaperRenderer::DrawDebugOverlays(const ImagePtr& output,
                                      const ImagePtr& depth,
                                      const ImagePtr& illumination) {
  if (show_debug_info_) {
    int32_t width = output->width();
    int32_t height = output->height();

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
    current_frame()->get().blitImage(
        illumination->get(), vk::ImageLayout::eShaderReadOnlyOptimal,
        output->get(), vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eLinear);
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
    blit.dstOffsets[0] = vk::Offset3D{width * 3 / 4, height / 4, 0};
    blit.dstOffsets[1] = vk::Offset3D{width, height / 2, 1};
    current_frame()->get().blitImage(
        depth->get(), vk::ImageLayout::eShaderReadOnlyOptimal, output->get(),
        vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eLinear);
  }
}

void PaperRenderer::DrawFrame(Stage& stage,
                              Model& model,
                              const ImagePtr& color_image_out,
                              const SemaphorePtr& frame_done,
                              FrameRetiredCallback frame_retired_callback) {
  // TODO: use a SRGB format for the lighting pass.
  UpdateModelRenderer(color_image_out->format(), vk::Format::eB8G8R8A8Unorm);

  uint32_t width = color_image_out->width();
  uint32_t height = color_image_out->height();

  BeginFrame();

  ImagePtr depth_image = image_cache_->NewDepthImage(
      depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  // Depth-only pre-pass.
  {
    FramebufferPtr prepass_fb = ftl::MakeRefCounted<Framebuffer>(
        escher_, width, height,
        std::vector<ImagePtr>{color_image_out, depth_image},
        model_renderer_->depth_prepass());

    current_frame()->AddUsedResource(prepass_fb);
    current_frame()->TakeWaitSemaphore(
        color_image_out, vk::PipelineStageFlagBits::eColorAttachmentOutput);

    DrawDepthPrePass(prepass_fb, stage, model);
    SubmitPartialFrame();
  }

  // Compute the illumination and store the result in a texture.
  TexturePtr illumination_texture;
  if (!enable_lighting_) {
    // If shadowing is disabled, use a single-pixel white texture to light each
    // pixel 100%.
    illumination_texture = model_renderer_->white_texture();
  } else {
    TexturePtr depth_texture = ftl::MakeRefCounted<Texture>(
        depth_image, context_.device, vk::Filter::eNearest,
        vk::ImageAspectFlagBits::eDepth);

    ImagePtr illum1 = image_cache_->NewImage(
        {impl::SsdoSampler::kColorFormat, width, height, 1,
         vk::ImageUsageFlagBits::eSampled |
             vk::ImageUsageFlagBits::eColorAttachment |
             vk::ImageUsageFlagBits::eTransferSrc});

    ImagePtr illum2 = image_cache_->NewImage(
        {impl::SsdoSampler::kColorFormat, width, height, 1,
         vk::ImageUsageFlagBits::eSampled |
             vk::ImageUsageFlagBits::eColorAttachment |
             vk::ImageUsageFlagBits::eTransferSrc});

    // We don't retain illum1/illum2 here because DrawSsdoPasses() wraps them
    // in Framebuffers, and retains those.
    current_frame()->AddUsedResource(depth_texture);

    DrawSsdoPasses(depth_texture, illum1, illum2, stage);

    SubmitPartialFrame();

    illumination_texture = ftl::MakeRefCounted<Texture>(illum1, context_.device,
                                                        vk::Filter::eNearest);

    // Done after previous SubmitPartialFrame(), because this is needed by the
    // final lighting pass.
    current_frame()->AddUsedResource(illumination_texture);
  }

  // Use multisampling for final lighting pass.
  {
    ImageInfo info;
    info.width = width;
    info.height = height;
    constexpr uint32_t kSampleCount = 4;
    info.sample_count = kSampleCount;

    // TODO: use Srgb, and make corresponding change to pipelines/render-passes.
    info.format = vk::Format::eB8G8R8A8Unorm;
    info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                 vk::ImageUsageFlagBits::eTransferSrc;
    ImagePtr color_image_multisampled = image_cache_->NewImage(info);

    // TODO: use lazily-allocated image: since we don't care about saving the
    // depth buffer, a tile-based GPU doesn't actually need this memory.
    info.format = depth_format_;
    info.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    ImagePtr depth_image_multisampled = image_cache_->NewImage(info);

    FramebufferPtr multisample_fb = ftl::MakeRefCounted<Framebuffer>(
        escher_, width, height, std::vector<ImagePtr>{color_image_multisampled,
                                                      depth_image_multisampled},
        model_renderer_->lighting_pass());

    current_frame()->AddUsedResource(multisample_fb);

    DrawLightingPass(multisample_fb, illumination_texture, stage, model);

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
    current_frame()->get().resolveImage(
        color_image_multisampled->get(),
        vk::ImageLayout::eColorAttachmentOptimal, color_image_out->get(),
        vk::ImageLayout::eColorAttachmentOptimal, resolve);
  }

  DrawDebugOverlays(color_image_out, depth_image,
                    illumination_texture->image());

  // ModelRenderer's lighting render-pass leaves the color-attachment format
  // as eColorAttachmentOptimal, since it's not clear how it will be used
  // next.
  // We could push this flexibility farther by letting our client specify the
  // desired output format, but for now we'll assume that the image is being
  // presented immediately.
  current_frame()->TransitionImageLayout(
      color_image_out, vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR);

  EndFrame(frame_done, frame_retired_callback);
}

}  // namespace escher
