// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/shadertoy/service/renderer.h"

#include <trace/event.h>

#include "src/ui/examples/shadertoy/service/compiler.h"
#include "src/ui/lib/escher/impl/command_buffer.h"
#include "src/ui/lib/escher/impl/mesh_manager.h"
#include "src/ui/lib/escher/impl/mesh_shader_binding.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/framebuffer.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/image_factory.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace shadertoy {

static vk::RenderPass CreateRenderPass(vk::Device device, vk::Format framebuffer_format) {
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
  input_dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  input_dependency.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
  input_dependency.dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
  input_dependency.dependencyFlags = vk::DependencyFlagBits::eByRegion;

  // The second dependency describes the transition from the initial to final
  // layout.
  output_dependency.srcSubpass = 0;  // our sole subpass
  output_dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
  output_dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
  output_dependency.dstStageMask = vk::PipelineStageFlagBits::eBottomOfPipe;
  output_dependency.srcAccessMask =
      vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
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
    FXL_LOG(ERROR) << "Failed to create Vulkan RenderPass.";
  }
  return result.value;
}

Renderer::Renderer(escher::EscherWeakPtr weak_escher, vk::Format framebuffer_format)
    : context_(weak_escher->vulkan_context()),
      escher_(std::move(weak_escher)),
      device_(escher()->vulkan_context().device),
      framebuffer_format_(framebuffer_format),
      render_pass_(CreateRenderPass(device_, framebuffer_format)),
      descriptor_set_pool_(escher()->GetWeakPtr(), Compiler::GetDescriptorSetLayoutCreateInfo()) {}

escher::Texture* Renderer::GetChannelTexture(const escher::FramePtr& frame,
                                             escher::Texture* texture_or_null) {
  if (!texture_or_null) {
    return white_texture_.get();
  }
  frame->cmds()->KeepAlive(texture_or_null);
  return texture_or_null;
}

vk::DescriptorSet Renderer::GetUpdatedDescriptorSet(const escher::FramePtr& frame,
                                                    escher::Texture* channel0,
                                                    escher::Texture* channel1,
                                                    escher::Texture* channel2,
                                                    escher::Texture* channel3) {
  TRACE_DURATION("gfx", "fuchsia::examples::shadertoy::Renderer::GetUpdatedDescriptorSet");

  constexpr uint32_t kChannelCount = 4;
  vk::DescriptorImageInfo channel_image_info[kChannelCount];
  vk::WriteDescriptorSet writes[kChannelCount];
  escher::Texture* textures[kChannelCount] = {channel0, channel1, channel2, channel3};
  auto descriptor_set = descriptor_set_pool_.Allocate(1, frame->cmds()->impl())->get(0);

  for (uint32_t i = 0; i < kChannelCount; ++i) {
    auto channel_texture = GetChannelTexture(frame, textures[i]);

    channel_image_info[i].imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    channel_image_info[i].imageView = channel_texture->vk_image_view();
    channel_image_info[i].sampler = channel_texture->sampler()->vk();

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

void Renderer::DrawFrame(const escher::FramebufferPtr& framebuffer, const PipelinePtr& pipeline,
                         const Params& params, escher::Texture* channel0, escher::Texture* channel1,
                         escher::Texture* channel2, escher::Texture* channel3,
                         escher::SemaphorePtr framebuffer_ready, escher::SemaphorePtr frame_done) {
  TRACE_DURATION("gfx", "fuchsia::examples::shadertoy::Renderer::DrawFrame");

  auto frame = escher()->NewFrame("Shadertoy Renderer", ++frame_number_);
  auto command_buffer = frame->cmds()->impl();
  auto vk_command_buffer = frame->vk_command_buffer();

  // Lazily initialize resources that need to be uploaded to the GPU; it's easiest to do here since
  // we have a command buffer to add the wait semaphore to.
  if (frame_number_ == 1) {
    escher::BatchGpuUploader gpu_uploader(escher()->GetWeakPtr(), frame_number_);
    full_screen_ = NewFullScreenMesh(escher()->mesh_manager(), &gpu_uploader);
    white_texture_ = CreateWhiteTexture(&gpu_uploader);
    auto upload_semaphore = escher::Semaphore::New(escher()->vk_device());
    gpu_uploader.AddSignalSemaphore(upload_semaphore);
    gpu_uploader.Submit();
    command_buffer->AddWaitSemaphore(
        std::move(upload_semaphore),
        vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eFragmentShader);
  }

  command_buffer->KeepAlive(framebuffer);
  command_buffer->AddWaitSemaphore(std::move(framebuffer_ready),
                                   vk::PipelineStageFlagBits::eColorAttachmentOutput);

  vk::Viewport viewport;
  viewport.width = framebuffer->width();
  viewport.height = framebuffer->height();
  vk_command_buffer.setViewport(0, 1, &viewport);

  auto descriptor_set = GetUpdatedDescriptorSet(frame, channel0, channel1, channel2, channel3);

  command_buffer->BeginRenderPass(
      render_pass_, framebuffer, {},
      escher::Camera::Viewport().vk_rect_2d(framebuffer->width(), framebuffer->height()));
  vk_command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->vk_pipeline());
  vk_command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       pipeline->vk_pipeline_layout(), 0, 1, &descriptor_set, 0,
                                       nullptr);
  vk_command_buffer.pushConstants(pipeline->vk_pipeline_layout(),
                                  vk::ShaderStageFlagBits::eFragment, 0, sizeof(Params), &params);

  // Draw full-screen mesh.
  {
    frame->cmds()->KeepAlive(full_screen_);

    const uint32_t vbo_binding = escher::impl::MeshShaderBinding::kTheOnlyCurrentlySupportedBinding;
    auto& attribute_buffer = full_screen_->attribute_buffer(vbo_binding);
    vk::Buffer vbo = attribute_buffer.vk_buffer;
    vk::DeviceSize vbo_offset = attribute_buffer.offset;
    vk_command_buffer.bindVertexBuffers(vbo_binding, 1, &vbo, &vbo_offset);
    vk_command_buffer.bindIndexBuffer(full_screen_->vk_index_buffer(),
                                      full_screen_->index_buffer_offset(), vk::IndexType::eUint32);
    vk_command_buffer.drawIndexed(full_screen_->num_indices(), 1, 0, 0, 0);
  }

  command_buffer->EndRenderPass();

  command_buffer->TransitionImageLayout(framebuffer->get_image(0),
                                        vk::ImageLayout::eColorAttachmentOptimal,
                                        vk::ImageLayout::ePresentSrcKHR);

  frame->EndFrame(frame_done, nullptr);
}

escher::TexturePtr Renderer::CreateWhiteTexture(escher::BatchGpuUploader* gpu_uploader) {
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;

  escher::ImageFactoryAdapter image_factory(escher()->gpu_allocator(),
                                            escher()->resource_recycler());
  auto image = escher::image_utils::NewRgbaImage(&image_factory, gpu_uploader, 1, 1, channels);

  return escher::Texture::New(escher()->resource_recycler(), std::move(image),
                              vk::Filter::eNearest);
}

Renderer::Params::Params()
    : iResolution(0.f),
      iTime(0.f),
      iTimeDelta(0.f),
      iFrame(0),
      iChannelTime{0.f, 0.f, 0.f, 0.f},
      iChannelResolution{glm::vec3(0), glm::vec3(0), glm::vec3(0), glm::vec3(0)},
      iMouse(0.f),
      iDate(0.f),
      iSampleRate(0.f) {}

}  // namespace shadertoy
