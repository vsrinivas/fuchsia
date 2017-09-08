// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/service/renderer.h"

#include "apps/mozart/examples/shadertoy/service/compiler.h"
#include "escher/geometry/tessellation.h"
#include "escher/impl/command_buffer.h"
#include "escher/impl/escher_impl.h"
#include "escher/impl/mesh_manager.h"
#include "escher/renderer/framebuffer.h"
#include "escher/renderer/image.h"
#include "escher/renderer/simple_image_factory.h"
#include "escher/renderer/texture.h"
#include "escher/util/image_utils.h"

using escher::Escher;

namespace shadertoy {

static vk::RenderPass CreateRenderPass(vk::Device device,
                                       vk::Format framebuffer_format) {
  constexpr uint32_t kAttachmentCount = 1;
  const uint32_t kColorAttachment = 0;
  vk::AttachmentDescription attachments[kAttachmentCount];
  auto& color_attachment = attachments[kColorAttachment];
  color_attachment.format = framebuffer_format;
  color_attachment.samples = vk::SampleCountFlagBits::e1;
  color_attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
  color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
  color_attachment.initialLayout = vk::ImageLayout::eUndefined;
  color_attachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference color_reference;
  color_reference.attachment = kColorAttachment;
  color_reference.layout = vk::ImageLayout::eColorAttachmentOptimal;

  // Every vk::RenderPass needs at least one subpass.
  vk::SubpassDescription subpass;
  subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.inputAttachmentCount = 0;  // no other subpasses to sample from

  // Even though we have a single subpass, we need to declare dependencies to
  // support the layout transitions specified by the attachment references.
  constexpr uint32_t kDependencyCount = 2;
  vk::SubpassDependency dependencies[kDependencyCount];
  auto& input_dependency = dependencies[0];
  auto& output_dependency = dependencies[1];

  // The first dependency transitions from the final layout from the previous
  // render pass, to the initial layout of this one.
  input_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;  // not in vulkan.hpp ?!?
  input_dependency.dstSubpass = 0;
  input_dependency.srcStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  input_dependency.dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  input_dependency.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                   vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency.srcSubpass = 0;  // our sole subpass
  output_dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency.srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
                                    vk::AccessFlagBits::eColorAttachmentWrite;
  output_dependency.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
  output_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // Create the render-pass.
  vk::RenderPassCreateInfo info;
  info.attachmentCount = kAttachmentCount;
  info.pAttachments = attachments;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = kDependencyCount;
  info.pDependencies = dependencies;

  auto result = device.createRenderPass(info);
  if (result.result != vk::Result::eSuccess) {
    FTL_LOG(ERROR) << "Failed to create Vulkan RenderPass.";
  }
  return result.value;
}

Renderer::Renderer(Escher* escher, vk::Format framebuffer_format)
    : escher::Renderer(escher),
      device_(escher->vulkan_context().device),
      framebuffer_format_(framebuffer_format),
      render_pass_(CreateRenderPass(device_, framebuffer_format)),
      full_screen_(NewFullScreenMesh(escher_impl()->mesh_manager())),
      white_texture_(CreateWhiteTexture()),
      descriptor_set_pool_(escher,
                           Compiler::GetDescriptorSetLayoutCreateInfo()) {}

escher::Texture* Renderer::GetChannelTexture(escher::Texture* texture_or_null) {
  if (!texture_or_null) {
    return white_texture_.get();
  }
  current_frame()->KeepAlive(texture_or_null);
  return texture_or_null;
}

vk::DescriptorSet Renderer::GetUpdatedDescriptorSet(escher::Texture* channel0,
                                                    escher::Texture* channel1,
                                                    escher::Texture* channel2,
                                                    escher::Texture* channel3) {
  constexpr uint32_t kChannelCount = 4;
  vk::DescriptorImageInfo channel_image_info[kChannelCount];
  vk::WriteDescriptorSet writes[kChannelCount];
  escher::Texture* textures[kChannelCount] = {channel0, channel1, channel2,
                                              channel3};
  auto descriptor_set =
      descriptor_set_pool_.Allocate(1, current_frame())->get(0);

  for (uint32_t i = 0; i < kChannelCount; ++i) {
    auto channel_texture = GetChannelTexture(textures[i]);

    channel_image_info[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    channel_image_info[i].imageView = channel_texture->image_view();
    channel_image_info[i].sampler = channel_texture->sampler();

    writes[i].dstSet = descriptor_set;
    writes[i].dstArrayElement = 0;
    writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
    writes[i].descriptorCount = 1;
    writes[i].dstBinding = i;
    writes[i].pImageInfo = channel_image_info + i;
  }

  device_.updateDescriptorSets(kChannelCount, writes, 0, nullptr);

  return descriptor_set;
}

void Renderer::DrawFrame(const escher::FramebufferPtr& framebuffer,
                         const PipelinePtr& pipeline,
                         const Params& params,
                         escher::Texture* channel0,
                         escher::Texture* channel1,
                         escher::Texture* channel2,
                         escher::Texture* channel3,
                         escher::SemaphorePtr framebuffer_ready,
                         escher::SemaphorePtr frame_done) {
  BeginFrame();

  current_frame()->KeepAlive(framebuffer);
  current_frame()->AddWaitSemaphore(
      std::move(framebuffer_ready),
      vk::PipelineStageFlagBits::eColorAttachmentOutput);

  auto vk_command_buffer = current_frame()->get();

  vk::Viewport viewport;
  viewport.width = framebuffer->width();
  viewport.height = framebuffer->height();
  vk_command_buffer.setViewport(0, 1, &viewport);

  auto descriptor_set =
      GetUpdatedDescriptorSet(channel0, channel1, channel2, channel3);

  current_frame()->BeginRenderPass(render_pass_, framebuffer, {});
  vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                                 pipeline->vk_pipeline());
  vk_command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       pipeline->vk_pipeline_layout(), 0, 1,
                                       &descriptor_set, 0, nullptr);
  vk_command_buffer.pushConstants(pipeline->vk_pipeline_layout(),
                                  vk::ShaderStageFlagBits::eFragment, 0,
                                  sizeof(Params), &params);
  current_frame()->DrawMesh(full_screen_);

  current_frame()->EndRenderPass();

  current_frame()->TransitionImageLayout(
      framebuffer->get_image(0), vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageLayout::ePresentSrcKHR);

  EndFrame(frame_done, nullptr);
}

escher::TexturePtr Renderer::CreateWhiteTexture() {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;

  escher::SimpleImageFactory image_factory(escher()->resource_recycler(),
                                           escher()->gpu_allocator());

  auto image = escher::image_utils::NewRgbaImage(
      &image_factory, escher()->gpu_uploader(), 1, 1, channels);
  return ftl::MakeRefCounted<escher::Texture>(
      escher()->resource_recycler(), std::move(image), vk::Filter::eNearest);
}

Renderer::Params::Params()
    : iResolution(0.f),
      iTime(0.f),
      iTimeDelta(0.f),
      iFrame(0),
      iChannelTime{0.f, 0.f, 0.f, 0.f},
      iChannelResolution{glm::vec3(0), glm::vec3(0), glm::vec3(0),
                         glm::vec3(0)},
      iMouse(0.f),
      iDate(0.f),
      iSampleRate(0.f) {}

}  // namespace shadertoy
