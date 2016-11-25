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
      // TODO: VulkanProvider should know the swapchain format and we should use
      // it.  Or, the PaperRenderer could be passed the format as a parameter.
      model_renderer_(std::make_unique<impl::ModelRenderer>(
          escher,
          model_data_.get(),
          vk::Format::eB8G8R8A8Srgb,
          ESCHER_CHECKED_VK_RESULT(
              impl::GetSupportedDepthFormat(context_.physical_device)))),
      ssdo_sampler_(std::make_unique<impl::SsdoSampler>(
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

void PaperRenderer::UpdateSsdoFramebuffer(const FramebufferPtr& framebuffer) {
  uint32_t width = framebuffer->width();
  uint32_t height = framebuffer->height();

  if (ssdo_framebuffer_ && width == ssdo_framebuffer_->width() &&
      height == ssdo_framebuffer_->height()) {
    // We already have a suitable framebuffer.
    return;
  }

  // Using eTransferSrc allows us to blit from the image: useful for debugging.
  ImagePtr color_image = escher_->image_cache()->NewColorAttachmentImage(
      width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  // Create the texture first, which saves us the trouble of creating a
  // separate ImageView for the Framebuffer (note: this also means that the
  // Framebuffer is not responsible for destroying it).
  ssdo_texture_ = ftl::MakeRefCounted<Texture>(color_image, context_.device,
                                               vk::Filter::eNearest);

  // Create Framebuffer with the attachment that we generated above.
  vk::FramebufferCreateInfo info;
  info.renderPass = ssdo_sampler_->render_pass();
  info.attachmentCount = 1;
  auto color_image_view = ssdo_texture_->image_view();
  info.pAttachments = &color_image_view;
  info.width = width;
  info.height = height;
  info.layers = 1;
  auto fb = ESCHER_CHECKED_VK_RESULT(context_.device.createFramebuffer(info));

  ssdo_framebuffer_ =
      CreateFramebuffer(fb, width, height, {color_image}, {}, nullptr);
}

FramebufferPtr PaperRenderer::NewFramebuffer(const ImagePtr& color_image) {
  vk::Device device = context_.device;
  uint32_t width = color_image->width();
  uint32_t height = color_image->height();

  ImagePtr depth_image = image_cache_->GetDepthImage(
      depth_format_, width, height,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);

  // Create ImageViews for all images, including the color image that we didn't
  // create.
  vk::ImageView color_image_view, depth_image_view;
  {
    vk::ImageViewCreateInfo info;
    info.viewType = vk::ImageViewType::e2D;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;

    // color_image
    info.format = color_image->format();
    info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    info.image = color_image->get();
    color_image_view = ESCHER_CHECKED_VK_RESULT(device.createImageView(info));

    // depth_image
    info.format = depth_format_;
    info.subresourceRange.aspectMask =
        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    info.image = depth_image->get();
    depth_image_view = ESCHER_CHECKED_VK_RESULT(device.createImageView(info));
  }

  // Create Framebuffer with the attachments that we generated above.
  vk::FramebufferCreateInfo framebuffer_create_info;
  std::vector<vk::ImageView> views{color_image_view, depth_image_view};
  framebuffer_create_info.renderPass = model_renderer_->depth_prepass();
  framebuffer_create_info.attachmentCount = static_cast<uint32_t>(views.size());
  framebuffer_create_info.pAttachments = views.data();
  framebuffer_create_info.width = width;
  framebuffer_create_info.height = height;
  framebuffer_create_info.layers = 1;

  // Create two frame buffers, one that references both the color and depth
  // images, and one that references only the color image.
  auto main_fb = ESCHER_CHECKED_VK_RESULT(
      device.createFramebuffer(framebuffer_create_info));
  framebuffer_create_info.renderPass = ssdo_sampler_->render_pass();
  framebuffer_create_info.attachmentCount = 1;
  auto extra_fb = ESCHER_CHECKED_VK_RESULT(
      device.createFramebuffer(framebuffer_create_info));

  auto extra = CreateFramebuffer(extra_fb, width, height, {color_image}, {});
  return CreateFramebuffer(main_fb, width, height, {color_image, depth_image},
                           views, extra);
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

void PaperRenderer::DrawSsdoPasses(const FramebufferPtr& framebuffer,
                                   Stage& stage) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer);

  // Prepare to sample from the depth buffer.
  // TODO: ideally we would not create a new texture every frame.
  auto depth_texture = ftl::MakeRefCounted<Texture>(
      framebuffer->get_image(kFramebufferDepthAttachmentIndex), context_.device,
      vk::Filter::eNearest, vk::ImageAspectFlagBits::eDepth);
  command_buffer->AddUsedResource(depth_texture);

  command_buffer->TransitionImageLayout(
      depth_texture->image(), vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal);

  impl::SsdoSampler::PushConstants push_constants(stage);
  ssdo_sampler_->Draw(command_buffer, ssdo_framebuffer_, depth_texture,
                      &push_constants, clear_values_);

  // Now that we have finished sampling the depth buffer, transition it for
  // reuse as a depth buffer in the lighting pass.
  command_buffer->TransitionImageLayout(
      depth_texture->image(), vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageLayout::eDepthStencilAttachmentOptimal);
}

void PaperRenderer::DrawLightingPass(const FramebufferPtr& framebuffer,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer);

  model_renderer_->hack_use_depth_prepass = false;
  command_buffer->BeginRenderPass(model_renderer_->lighting_pass(), framebuffer,
                                  clear_values_);
  model_renderer_->Draw(stage, model, command_buffer, ssdo_texture_);
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
        ssdo_framebuffer_->get_image(0)->get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        framebuffer->get_image(kFramebufferColorAttachmentIndex)->get(),
        vk::ImageLayout::eColorAttachmentOptimal, 1, &blit,
        vk::Filter::eLinear);
    blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
    blit.dstOffsets[0] = vk::Offset3D{width * 3 / 4, height / 4, 0};
    blit.dstOffsets[1] = vk::Offset3D{width, height / 2, 1};
    command_buffer->get().blitImage(
        framebuffer->get_image(kFramebufferDepthAttachmentIndex)->get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        framebuffer->get_image(kFramebufferColorAttachmentIndex)->get(),
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
      framebuffer->get_image(0), vk::ImageLayout::eColorAttachmentOptimal,
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

  UpdateSsdoFramebuffer(framebuffer);
  DrawSsdoPasses(framebuffer, stage);
  SubmitPartialFrame();

  DrawLightingPass(framebuffer, stage, model);
  EndFrame(frame_done, frame_retired_callback);
}

}  // namespace escher
