// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/renderer/paper_renderer.h"

#include "escher/impl/command_buffer.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/image_cache.h"
#include "escher/impl/model_data.h"
#include "escher/impl/model_pipeline_cache.h"
#include "escher/impl/model_renderer.h"
#include "escher/impl/render_pass_manager.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"

namespace escher {

PaperRenderer::PaperRenderer(impl::EscherImpl* escher)
    : Renderer(escher),
      image_cache_(escher->image_cache()),
      // TODO: perhaps cache depth_format_ in EscherImpl.
      depth_format_(ESCHER_CHECKED_VK_RESULT(
          impl::GetSupportedDepthFormat(context_.physical_device))),
      render_pass_(escher->render_pass_manager()->GetPaperRendererRenderPass()),
      // TODO: could potentially share ModelData/PipelineCache/ModelRenderer
      // between multiple PaperRenderers.
      model_data_(std::make_unique<impl::ModelData>(context_.device,
                                                    escher->gpu_allocator())),
      model_pipeline_cache_(
          std::make_unique<impl::ModelPipelineCache>(context_.device,
                                                     render_pass_,
                                                     0,
                                                     model_data_.get())),
      model_renderer_(
          std::make_unique<impl::ModelRenderer>(escher,
                                                model_data_.get(),
                                                model_pipeline_cache_.get())) {}

PaperRenderer::~PaperRenderer() {}

FramebufferPtr PaperRenderer::NewFramebuffer(const ImagePtr& image) {
  uint32_t width = image->width();
  uint32_t height = image->height();
  // TODO: the depth image obtained from ImageCache doesn't specify "Sampled"
  // and/or "Storage"... probably need to change this when we implement SSAO.
  auto depth_image = image_cache_->GetDepthImage(depth_format_, width, height);
  vk::Device device = context_.device;

  vk::ImageViewCreateInfo view_create_info;
  view_create_info.viewType = vk::ImageViewType::e2D;
  view_create_info.subresourceRange.baseMipLevel = 0;
  view_create_info.subresourceRange.levelCount = 1;
  view_create_info.subresourceRange.baseArrayLayer = 0;
  view_create_info.subresourceRange.layerCount = 1;

  // Create ImageView for color image.
  view_create_info.format = image->format();
  view_create_info.subresourceRange.aspectMask =
      vk::ImageAspectFlagBits::eColor;
  view_create_info.image = image->get();
  vk::ImageView image_view =
      ESCHER_CHECKED_VK_RESULT(device.createImageView(view_create_info));

  // Create ImageView for depth image.
  view_create_info.format = depth_format_;
  view_create_info.subresourceRange.aspectMask =
      vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
  view_create_info.image = depth_image->get();
  vk::ImageView depth_image_view =
      ESCHER_CHECKED_VK_RESULT(device.createImageView(view_create_info));

  // Create Framebuffer with two attachments.
  vk::FramebufferCreateInfo framebuffer_create_info;
  std::vector<vk::ImageView> views{image_view, depth_image_view};
  framebuffer_create_info.renderPass = render_pass_;
  framebuffer_create_info.attachmentCount = static_cast<uint32_t>(views.size());
  framebuffer_create_info.pAttachments = views.data();
  framebuffer_create_info.width = width;
  framebuffer_create_info.height = height;
  framebuffer_create_info.layers = 1;

  auto fb = ESCHER_CHECKED_VK_RESULT(
      device.createFramebuffer(framebuffer_create_info));

  return CreateFramebuffer(fb, width, height,
                           std::vector<ImagePtr>{image, depth_image}, views);
}

void PaperRenderer::BeginModelRenderPass(const FramebufferPtr& framebuffer,
                                         vk::CommandBuffer command_buffer) {
  uint32_t width = framebuffer->width();
  uint32_t height = framebuffer->height();

  vk::ClearValue clear_values[2];
  clear_values[0] =
      vk::ClearColorValue(std::array<float, 4>{{0.012, 0.047, 0.427, 1.f}});
  clear_values[1] = vk::ClearDepthStencilValue{1.f, 0};

  vk::RenderPassBeginInfo info;
  info.renderPass = render_pass_;
  info.renderArea.offset.x = 0;
  info.renderArea.offset.y = 0;
  info.renderArea.extent.width = width;
  info.renderArea.extent.height = height;
  info.clearValueCount = 2;
  info.pClearValues = clear_values;
  info.framebuffer = framebuffer->framebuffer();

  command_buffer.beginRenderPass(&info, vk::SubpassContents::eInline);

  vk::Viewport viewport;
  viewport.width = static_cast<float>(width);
  viewport.height = static_cast<float>(height);
  viewport.minDepth = static_cast<float>(0.0f);
  viewport.maxDepth = static_cast<float>(1.0f);
  command_buffer.setViewport(0, 1, &viewport);

  // TODO: probably unnecessary?
  vk::Rect2D scissor;
  scissor.extent.width = width;
  scissor.extent.height = height;
  scissor.offset.x = 0;
  scissor.offset.y = 0;
  command_buffer.setScissor(0, 1, &scissor);
}

void PaperRenderer::DrawFrame(Stage& stage,
                              Model& model,
                              const FramebufferPtr& framebuffer,
                              const SemaphorePtr& frame_done,
                              FrameRetiredCallback frame_retired_callback) {
  impl::CommandBuffer* command_buffer = BeginFrame(framebuffer, frame_done);

  BeginModelRenderPass(framebuffer, command_buffer->get());
  model_renderer_->Draw(stage, model, command_buffer);
  command_buffer->get().endRenderPass();

  EndFrame(std::move(frame_retired_callback));
}

}  // namespace escher
