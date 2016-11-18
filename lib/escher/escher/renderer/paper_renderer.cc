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
          vk::Format::eB8G8R8A8Unorm,
          ESCHER_CHECKED_VK_RESULT(
              impl::GetSupportedDepthFormat(context_.physical_device)))),
      clear_values_({vk::ClearColorValue(
                         std::array<float, 4>{{0.012, 0.047, 0.427, 1.f}}),
                     vk::ClearDepthStencilValue(1.f, 0)}) {}

PaperRenderer::~PaperRenderer() {}

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
  auto fb = ESCHER_CHECKED_VK_RESULT(
      device.createFramebuffer(framebuffer_create_info));
  return CreateFramebuffer(fb, width, height, {color_image, depth_image},
                           views);
}

void PaperRenderer::DrawDepthPrePass(const FramebufferPtr& framebuffer,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer);

  model_renderer_->hack_use_depth_prepass = true;
  command_buffer->BeginRenderPass(model_renderer_->depth_prepass(), framebuffer,
                                  clear_values_);
  model_renderer_->Draw(stage, model, command_buffer);
  command_buffer->EndRenderPass();
}

void PaperRenderer::DrawLightingPass(const FramebufferPtr& framebuffer,
                                     Stage& stage,
                                     Model& model) {
  auto command_buffer = current_frame();
  command_buffer->AddUsedResource(framebuffer);

  model_renderer_->hack_use_depth_prepass = false;
  command_buffer->BeginRenderPass(model_renderer_->lighting_pass(), framebuffer,
                                  clear_values_);
  model_renderer_->Draw(stage, model, command_buffer);
  command_buffer->EndRenderPass();

  vk::ImageBlit blit;
  blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
  blit.srcSubresource.mipLevel = 0;
  blit.srcSubresource.baseArrayLayer = 0;
  blit.srcSubresource.layerCount = 1;
  blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
  blit.srcOffsets[1] = vk::Offset3D{1024, 1024, 1};
  blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  blit.dstSubresource.mipLevel = 0;
  blit.dstSubresource.baseArrayLayer = 0;
  blit.dstSubresource.layerCount = 1;
  blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
  blit.dstOffsets[1] = vk::Offset3D{256, 256, 1};
  command_buffer->get().blitImage(
      framebuffer->get_image(1)->get(),
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      framebuffer->get_image(0)->get(),
      vk::ImageLayout::eColorAttachmentOptimal, 1, &blit, vk::Filter::eLinear);

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
  DrawLightingPass(framebuffer, stage, model);
  EndFrame(frame_done, frame_retired_callback);
}

}  // namespace escher
